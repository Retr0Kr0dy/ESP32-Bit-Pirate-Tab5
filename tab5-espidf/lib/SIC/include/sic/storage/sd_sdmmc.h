#pragma once
#include <stdint.h>
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

#ifdef __cplusplus
}
#endif
