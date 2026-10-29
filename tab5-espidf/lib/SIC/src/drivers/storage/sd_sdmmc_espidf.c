/*
 * sd_sdmmc_espidf.c — ESP-IDF SDMMC (4-bit) SD card driver, registered
 * under the existing SIC_F_SD function ID alongside sd_spi_arduino.cpp.
 *
 * Genuinely ESP-IDF-only: there is no Arduino SDMMC equivalent in this
 * codebase (unlike the shared bus/gpio/i2c/delay/audio contracts), so
 * unlike those, this file has no Arduino counterpart to keep in sync with.
 * Same accepted "backend-specific code living directly under src/drivers/"
 * exception sd_spi_arduino.cpp already uses, per
 * docs/DESIGN_INVARIANTS.md's platform-isolation section (drivers/ is
 * normally pure C99, but SD has no universal bus abstraction to sit
 * behind).
 *
 * Uses ESP-IDF's driver/sdmmc_host.h + sdmmc_cmd.h + esp_vfs_fat.h
 * (esp_vfs_fat_sdmmc_mount) for begin(), and — when cfg->ldo_chan >= 0 —
 * sd_pwr_ctrl_by_on_chip_ldo.h to power the SD IO rail from an on-chip LDO
 * channel before mounting (Tab5 uses LDO channel 4 @ 3300mV). Struct/function names below were verified
 * against the real ESP-IDF v5.5.4 header tree
 * (~/.platformio/packages/framework-espidf) for an ESP32-P4 target, not
 * guessed — including a full standalone compile check of the exact
 * sdmmc_host_t/sdmmc_slot_config_t/sd_pwr_ctrl_ldo_config_t/
 * esp_vfs_fat_mount_config_t/sdmmc_card_t usage below against that tree,
 * the same verification standard Phase 1/2/7 used. Notably,
 * SDMMC_SLOT_CONFIG_DEFAULT() for CONFIG_IDF_TARGET_ESP32P4 already
 * defaults clk/cmd/d0-d3 to GPIO 43/44/39/40/41/42, which are exactly
 * Tab5's documented SD pins — this driver still takes pins
 * from sic_sd_sdmmc_cfg_t rather than relying on that coincidence, so it
 * stays correct for any future board with different SDMMC pins.
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#include "sic/sic.h"
#include "sic/storage/sd.h"
#include "sic/storage/sd_sdmmc.h"
#include "sic/sic_registry.h"

#define SD_SDMMC_MOUNT_POINT "/sdcard"
#define SD_SDMMC_MAX_FILES   5
#define SD_SDMMC_ALLOC_UNIT  (16 * 1024)

typedef struct {
    sic_sd_sdmmc_cfg_t cfg;
    sd_pwr_ctrl_handle_t pwr_handle;
    sdmmc_card_t*         card;
    int64_t               last_attempt_us;
    int                   mounted;
} sd_sdmmc_ctx_t;

static sd_sdmmc_ctx_t g_ctx;
static sd_t            g_sd;
static bool            g_usb_owns_sd = false;

// Serializes every operation that reads-then-acts on g_usb_owns_sd/g_ctx
// against the SD card hardware: sd_sdmmc_begin_locked() below (the full
// local mount attempt) and sic_sd_sdmmc_claim_for_usb() (USB's ownership
// claim + local release). Without this, a check-then-act gap between the
// two sides -- local reads g_usb_owns_sd==false and starts mounting, USB
// concurrently sets it true and releases the local mount out from under
// that in-progress attempt -- let both sides reach for the same physical
// card at once. Static allocation (no heap, no lazy-init race on first
// use): the StaticSemaphore_t buffer backs the handle for the life of the
// program.
static StaticSemaphore_t g_owner_mutex_buf;
static SemaphoreHandle_t g_owner_mutex = NULL;

static SemaphoreHandle_t owner_mutex(void) {
    if (!g_owner_mutex) g_owner_mutex = xSemaphoreCreateMutexStatic(&g_owner_mutex_buf);
    return g_owner_mutex;
}

void sic_sd_sdmmc_mark_usb_owner(bool owned) { g_usb_owns_sd = owned; }
bool sic_sd_sdmmc_is_usb_owner(void) { return g_usb_owns_sd; }

// USB's atomic claim: check-and-set g_usb_owns_sd plus the local release,
// all under the same lock sd_sdmmc_begin_locked() takes for its own full
// mount attempt -- see owner_mutex()'s own comment for the race this
// closes. Replaces the separate sic_sd_sdmmc_mark_usb_owner(true) +
// sic_sd_sdmmc_release_for_usb() pair usb_msc_attach_sdcard() used to call
// back to back with nothing serializing them against a concurrent local
// mount. Returns 0 and leaves ownership marked true on success (matching
// sic_sd_sdmmc_release_for_usb()'s own 0/-1 contract); on failure, rolls
// ownership back to false before returning -1 -- the caller doesn't need
// its own separate rollback call for this specific step anymore (its
// later failure paths, after this returns successfully, still do).
int sic_sd_sdmmc_claim_for_usb(void) {
    xSemaphoreTake(owner_mutex(), portMAX_DELAY);
    g_usb_owns_sd = true;
    int rc = sic_sd_sdmmc_release_for_usb();
    if (rc != 0) g_usb_owns_sd = false;
    xSemaphoreGive(owner_mutex());
    return rc;
}

static int sd_sdmmc_begin_locked(const void* self) {
    sd_sdmmc_ctx_t* c = (sd_sdmmc_ctx_t*)((const sd_t*)self)->impl;
    if (!c) return -1;
    if (c->mounted) return 0;
    // USB MSC currently has this card open on its own independent SDMMC
    // session (slot 0, same pins) -- mounting a second, local FAT session
    // on top of that races two caches against the same physical card. See
    // sic_sd_sdmmc_mark_usb_owner()'s own header comment for the full
    // handoff story this closes.
    if (g_usb_owns_sd) return -1;

    /* Orion is now a standalone product UI: an SD card is optional at boot
     * and may be inserted later for media or a hot-loaded theme.  The old
     * bring-up behavior permanently latched the first failed mount until
     * reboot, which made that impossible.  Retry failures, but rate-limit
     * them so a screen polling present() cannot hammer SDMMC/LDO setup. */
    int64_t now_us = esp_timer_get_time();
    if (c->last_attempt_us && now_us - c->last_attempt_us < 1000000) return -1;
    c->last_attempt_us = now_us;

    if (c->cfg.ldo_chan >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = c->cfg.ldo_chan };
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &c->pwr_handle) != ESP_OK) {
            c->pwr_handle = NULL;
            return -1;
        }
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.pwr_ctrl_handle = c->pwr_handle;
    /* SDMMC_HOST_DEFAULT()
     * hardcodes .slot = SDMMC_HOST_SLOT_1 (see
     * esp_driver_sdmmc/include/driver/sdmmc_default_configs.h). Tab5-Orion's
     * esp-hosted integration (wifi.c, ESP32-C6 SDIO link) ALSO hardcodes
     * H_SDMMC_HOST_SLOT to SDMMC_HOST_SLOT_1 in the managed component itself
     * (esp_hosted_config.h) with no Kconfig override -- so the SD card and
     * the WiFi co-processor were silently sharing one of the ESP32-P4's two
     * independent SDMMC host peripheral instances (SOC_SDMMC_NUM_SLOTS == 2)
     * despite using entirely different GPIO pins. Whichever side touched the
     * shared controller's registers last corrupted the other's in-flight
     * transactions -- root cause of both a catastrophic SD read throughput
     * collapse (25fps video down to ~1fps once WiFi was also active) and the
     * esp-hosted "failed to read registers" retry storm after "wifi up".
     * Since esp-hosted's slot is fixed and not ours to change, the SD driver
     * moves to the other slot instead -- pins are still fully explicit below
     * (ESP32-P4 supports GPIO-matrix routing on both slots), so this is a
     * pure hardware-instance reassignment, not a pin change. */
    host.slot = SDMMC_HOST_SLOT_0;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk    = (gpio_num_t)c->cfg.clk_pin;
    slot.cmd    = (gpio_num_t)c->cfg.cmd_pin;
    slot.d0     = (gpio_num_t)c->cfg.d0_pin;
    slot.d1     = (gpio_num_t)c->cfg.d1_pin;
    slot.d2     = (gpio_num_t)c->cfg.d2_pin;
    slot.d3     = (gpio_num_t)c->cfg.d3_pin;
    slot.width  = 4; /* Tab5 (and every board this driver targets so far) wires D0-D3 only. */

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = SD_SDMMC_MAX_FILES,
        .allocation_unit_size   = SD_SDMMC_ALLOC_UNIT,
    };

    sdmmc_card_t* card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_SDMMC_MOUNT_POINT, &host, &slot, &mount_cfg, &card);
    if (err != ESP_OK || !card) {
        if (c->pwr_handle) {
            sd_pwr_ctrl_del_on_chip_ldo(c->pwr_handle);
            c->pwr_handle = NULL;
        }
        return -1;
    }

    c->card    = card;
    c->mounted = 1;
    return 0;
}

// Public entry point (keeps the exact name/signature every existing caller
// and the g_sd vtable already use) -- just adds the same lock
// sic_sd_sdmmc_claim_for_usb() takes around the real logic above, so a
// local mount attempt and a concurrent USB ownership claim can't interleave.
static int sd_sdmmc_begin(const void* self) {
    xSemaphoreTake(owner_mutex(), portMAX_DELAY);
    int rc = sd_sdmmc_begin_locked(self);
    xSemaphoreGive(owner_mutex());
    return rc;
}

static int sd_sdmmc_present(const void* self) {
    const sd_t* sd = (const sd_t*)self;
    sd_sdmmc_ctx_t* c = sd ? (sd_sdmmc_ctx_t*)sd->impl : NULL;
    if (!c) return 0;
    if (!c->mounted && sd_sdmmc_begin(self) != 0) return 0;
    return c->mounted && c->card ? 1 : 0;
}

static uint64_t sd_sdmmc_card_size(const void* self) {
    if (!sd_sdmmc_present(self)) return 0;
    const sd_t* sd = (const sd_t*)self;
    sd_sdmmc_ctx_t* c = (sd_sdmmc_ctx_t*)sd->impl;
    return (uint64_t)c->card->csd.capacity * (uint64_t)c->card->csd.sector_size;
}

static const struct sd_vtbl_s SD_SDMMC_VT = {
    sd_sdmmc_begin,
    sd_sdmmc_present,
    sd_sdmmc_card_size
};

static int probe_sd_sdmmc(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "sd_sdmmc") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_sd_sdmmc_cfg_t*)d->cfg;
    if (g_ctx.cfg.clk_pin < 0 || g_ctx.cfg.cmd_pin < 0 || g_ctx.cfg.d0_pin < 0 ||
        g_ctx.cfg.d1_pin < 0 || g_ctx.cfg.d2_pin < 0 || g_ctx.cfg.d3_pin < 0) return -1;
    g_sd.v    = &SD_SDMMC_VT;
    g_sd.impl = &g_ctx;
    *out = &g_sd;
    return 0;
}

int sic_sd_sdmmc_release_for_usb(void) {
    if (!g_ctx.mounted) return 0;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_SDMMC_MOUNT_POINT, g_ctx.card);
    if (err != ESP_OK) {
        // Unmount failed -- leave g_ctx exactly as it was (still "mounted",
        // still the same card/power handle) instead of discarding state the
        // caller didn't actually win. The caller (usb_msc_attach_sdcard())
        // already refuses to proceed to MSC on a nonzero return; this is
        // what makes that refusal actually mean "local still owns it"
        // rather than leaving both sides thinking nobody does. Previously
        // this cleared g_ctx unconditionally even on failure -- confirmed
        // wrong, see B/HARDWARE_TEST_TRACKER.md's SD/MSC ownership finding.
        return -1;
    }
    g_ctx.card = NULL;
    g_ctx.mounted = 0;
    /* esp_vfs_fat_sdcard_unmount() tears down the host+card it was given,
     * but NOT this LDO handle (a separate resource sd_sdmmc_begin() created
     * itself) -- free it explicitly so the USB side's own temporary LDO
     * handle on the same channel doesn't collide with a still-live one, and
     * so sd_sdmmc_begin()'s unconditional sd_pwr_ctrl_new_on_chip_ldo() call
     * (it has no "already have one" guard -- written assuming a fresh boot)
     * creates a clean new handle on the next normal mount instead of leaking
     * or erroring against this stale one. */
    if (g_ctx.pwr_handle) {
        sd_pwr_ctrl_del_on_chip_ldo(g_ctx.pwr_handle);
        g_ctx.pwr_handle = NULL;
    }
    return 0;
}

const sic_sd_sdmmc_cfg_t* sic_sd_sdmmc_last_cfg(void) {
    return g_ctx.cfg.clk_pin > 0 ? &g_ctx.cfg : NULL;
}

static const sic_driver_t DRV_SD_SDMMC = { "sd_sdmmc", SIC_F_SD, probe_sd_sdmmc, NULL };

void sic_register_driver_sd_sdmmc(void) {
    sic_registry_register(&DRV_SD_SDMMC);
}

#endif /* SIC_BACKEND_ESPIDF */
