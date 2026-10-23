/*
 * main.c — ESP32-Bit-Pirate-P4-Tab5 entry point.
 *
 * Native IDF board bring-up (SIC + MIPI-DSI display), then hands off to
 * bitpirate_task_entry() (Arduino-as-component task running the real GUI
 * and GenericBusDispatcher).
 *
 * Boot logging is plain ESP_LOG, not lib/konsole: konsole is a REPL/shell
 * engine meant to stand in for a target's own interactive console on
 * non-ESP boards. This firmware only ever ships on ESP32-P4 and already
 * has ESP-IDF's own logging (and, if a future pass wants an interactive
 * serial console, esp_console) — carrying a second vendored console
 * library for a handful of boot-time log lines bought nothing. The GUI's
 * interactive input/output (touchscreen + the real keyboard accessory)
 * goes through LVGL (see Tab5Gui.h) instead, not konsole either — see that
 * file's header for why.
 *
 * Bring-up order (mirrors Tab5-Launchpad/main/main.c):
 *   1. NVS.
 *   2. SIC      — sic_i2c_begin_bus() on bus 0, then sic_begin_legacy().
 *   3. SGFX     — orion_display_init() (must come after SIC: panel choice
 *                 reads SIC's cached panel identity).
 *   4. Landscape-only rotation (see Tab5Gui.h/Tab5Layout.h), then splash,
 *      then the real Arduino-as-component task.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"
#include "boards/tab5/ioexpander.h"

#include "display.h"
#include "boards/tab5_bitpirate/BitPirateTask.h"

static const char* TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-Bit-Pirate-P4-Tab5 ===");
    ESP_LOGI(TAG, "build: %s %s (IDF %s)", __DATE__, __TIME__, esp_get_idf_version());

    /* NVS -- not strictly needed by anything in this phase yet (no WiFi/BT,
     * no settings persistence wired up), but every later phase needs it and
     * initializing it once, early, is the standard ESP-IDF bring-up order. */
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_rc = nvs_flash_init();
    }
    ESP_LOGI(TAG, "[NVS] init rc=%d", (int)nvs_rc);

    /* SIC */
    int rc = sic_i2c_begin_bus(TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, TAB5_I2C_HZ);
    ESP_LOGI(TAG, "[SIC] i2c bus %d SDA=%d SCL=%d @%uHz rc=%d",
             TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, (unsigned)TAB5_I2C_HZ, rc);

    rc = sic_begin_legacy(&SIC_BOARD_TAB5, NULL);
    ESP_LOGI(TAG, "[SIC] board=%s begin rc=%d", SIC_BOARD_TAB5.name, rc);

    for (int f = 0; f < (int)SIC_F__COUNT; ++f) {
        sic_func_id_t fn = (sic_func_id_t)f;
        int n = sic_count_fn(fn);
        if (n <= 0) continue;
        ESP_LOGI(TAG, "[SIC] %s(%s)", sic_func_name(fn), sic_name_fn(fn, 0));
    }

    /* SGFX / display (used by Tab5Gui's LVGL flush bridge, and by the
     * splash below, as the actual pixel-pushing layer under LVGL now --
     * see Tab5Gui.h's header comment). */
    rc = orion_display_init();
    ESP_LOGI(TAG, "[GFX] orion_display_init rc=%d ready=%d panel=%s",
             rc, orion_display_ready(), orion_panel_name());

    /* USB5V_EN (IO-expander 1, bit 3): driven HIGH by SIC's generic Tab5
     * board bring-up above, meant for a future USB HOST mode. This phase
     * has no USB personality at all yet, so leave it exactly as SIC set
     * it -- revisit when Phase 6 (USB HID/flashrom/storage) adds a real
     * USB role and needs to decide host vs. device VBUS behavior, same
     * reasoning Tab5-Launchpad's main.c documents for its own (device-
     * mode) case. */

    /* Landscape-only for now (see Tab5Layout.h for the full reasoning):
     * GUI layout has one set of region constants, not rotation-aware the
     * way Orion's own menu system is, so the generic 4-way
     * orion_display_poll_auto_rotation() (on by default) stays off --
     * it would happily rotate into a portrait mode this GUI can't render.
     * Rotation 1 is Tab5-Orion's own "upright landscape" convention on
     * this exact panel (doom_platform.c's s_doom_rot=1) -- the FALLBACK
     * default when the real orientation can't be read at boot (see below);
     * display.c's logical_to_phys()/phys_to_logical() then make every draw
     * call AND touch_bridge.c's touch mapping agree automatically, with no
     * separate transform needed in the GUI layer, regardless of which of
     * the two landscape rotations actually gets committed.
     *
     * The keyboard accessory docks the tablet in the OTHER landscape
     * orientation (confirmed by physical testing: UI renders upside-down
     * once attached, never correcting) -- orion_display_poll_auto_rotation_
     * landscape(), polled from this task's own idle loop below, is a
     * landscape-only variant (same IMU read/debounce, restricted to
     * committing rotation 1 or 3 only, see its own comment in display.c)
     * that flips between the two automatically without touching the
     * generic 4-way function's off switch above. */
    if (orion_display_ready()) {
        orion_display_set_auto_rotation(0);
        /* Read the real IMU orientation at boot instead of always starting
         * at a hardcoded default, so a device already docked/held in the
         * "flipped" landscape orientation comes up correct immediately
         * rather than waiting for the auto-rotation poller's debounce.
         * Falls back to rotation 1 when the reading is ambiguous. */
        int bootRot = orion_display_detect_boot_rotation_landscape();
        orion_display_set_rotation(bootRot > 0 ? bootRot : 1);
    }

    /* Splash, then the real task (Arduino-as-component + Tab5Gui/LVGL +
     * GenericBusDispatcher). Held for a fixed 3s -- matching the official
     * p4-tab5 branch's own deviceView.logo() + waitPress(3000) -- since
     * the LVGL GUI starting inside bitpirate_task_entry() overwrites the
     * splash within a few hundred ms otherwise (measured on real hardware:
     * ~300ms from orion_display_splash() to tab5gui::begin() done). No
     * touch/key input exists yet at this point to end it early the way
     * waitPress() does, so this is a plain, unconditional hold. */
    if (orion_display_ready()) {
        orion_display_splash();
        // Poll rotation during the hold instead of one blocking delay, so a
        // late IMU reading can still correct the splash before it disappears.
        int heldMs = 0, lastRot = orion_display_get_rotation();
        while (heldMs < 3000) {
            vTaskDelay(pdMS_TO_TICKS(100));
            heldMs += 100;
            orion_display_poll_auto_rotation_landscape();
            int rot = orion_display_get_rotation();
            if (rot != lastRot) { lastRot = rot; orion_display_splash(); }
        }
    }

    xTaskCreate(bitpirate_task_entry, "bitpirate_task", 32768, NULL, 5, NULL);

    for (;;) {
        orion_display_poll_auto_rotation_landscape();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
