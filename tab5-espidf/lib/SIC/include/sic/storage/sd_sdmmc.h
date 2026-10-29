#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct sic_sd_sdmmc_cfg_s {
    int clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
    int ldo_chan;     /* on-chip LDO channel powering the SD IO rail, or -1 to skip LDO management */
    uint32_t ldo_mv;  /* LDO output voltage in mV, ignored if ldo_chan < 0 */
} sic_sd_sdmmc_cfg_t;

/*
 * USB mass-storage handoff (main/usb_device.c): the SD card can only have
 * ONE FatFs mount at a time -- exposing it to a USB host while SIC's own
 * "/sdcard" VFS mount is also live would race two independent FAT caches
 * against the same card. sic_sd_sdmmc_release_for_usb() fully releases
 * SIC's mount (esp_vfs_fat_sdcard_unmount() tears down the host+card+VFS
 * registration together -- there is no finer-grained "detach FS, keep card"
 * primitive in ESP-IDF's FAT VFS layer) and frees the LDO power handle, so
 * the USB side can do its own independent sdmmc_host_init()+
 * sdmmc_card_init() on the same pins/power rail and hand that card to
 * tinyusb's MSC storage component. SIC's own mount is NOT proactively
 * restored afterward: its existing lazy sd_sdmmc_begin() path (already
 * idempotent, already handles "not mounted" by remounting from scratch)
 * just runs again next time something touches the SD card normally (e.g.
 * the file browser's SD tab) -- exactly like a fresh card insertion.
 */
int sic_sd_sdmmc_release_for_usb(void);              /* 0 on success (or nothing to release), -1 on error */
const sic_sd_sdmmc_cfg_t* sic_sd_sdmmc_last_cfg(void); /* pins for the last configured card, or NULL if never configured */

/*
 * Ownership flag for the handoff above, and the atomic claim that sets it.
 *
 * sic_sd_sdmmc_claim_for_usb() is the real entry point for the USB side:
 * atomically (under the same lock sd_sdmmc_begin()'s full local mount
 * attempt takes) marks USB as owner, then calls sic_sd_sdmmc_release_for_usb()
 * -- rolling the flag back to false if that release fails. Call it once,
 * in place of calling sic_sd_sdmmc_release_for_usb() and
 * sic_sd_sdmmc_mark_usb_owner(true) separately: those two used to run back
 * to back with nothing serializing them against a concurrent local mount
 * attempt (a local sd_sdmmc_begin() could read the flag as still false and
 * start mounting in the gap between them), which this closes.
 *
 * sic_sd_sdmmc_mark_usb_owner() stays available directly for the caller's
 * own *later* rollback paths -- clearing ownership again if something
 * after the claim (LDO power-up, host/slot/card init, the tinyusb MSC
 * storage call) fails, and for usb_msc_detach_sdcard()'s own teardown once
 * USB is done with the card. It does not need the lock itself: by the time
 * any of those run, the claim already completed (or never started), so
 * there's no concurrent local mount attempt still in flight to race.
 *
 * sd_sdmmc_begin() (the local lazy-remount path) refuses to run while
 * ownership is held by USB, instead of racing a second, independent
 * FAT/SDMMC session onto the same physical card USB currently has open --
 * the hazard this handoff was always documented as needing but never
 * actually enforced; see the handoff comment above and
 * B/HARDWARE_TEST_TRACKER.md's SD/MSC ownership finding for the concrete
 * bug trail, including the race this atomic version closes that a plain
 * mark-then-release pair didn't.
 */
int sic_sd_sdmmc_claim_for_usb(void);  /* 0 on success (ownership held), -1 on error (ownership not held) */
void sic_sd_sdmmc_mark_usb_owner(bool owned);
bool sic_sd_sdmmc_is_usb_owner(void);

#ifdef __cplusplus
}
#endif
