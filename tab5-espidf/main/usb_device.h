#pragma once
/*
 * usb_device.h — native ESP-IDF USB device-mode bridge (HID keyboard/mouse/
 * gamepad/system-control + MSC mass storage), replacing the Arduino-ESP32
 * USB.h/USBHIDKeyboard.h/etc. stack that UsbS3Service.cpp used to call.
 *
 * Why this exists: arduino-esp32 3.3.12's USB.h/USBHID*.h compile their real
 * bodies out entirely against the esp_tinyusb version this project is
 * pinned to (CONFIG_TINYUSB_ENABLED, the symbol they gate on, is never
 * defined by this esp_tinyusb release -- confirmed by grepping every
 * Kconfig in the dependency tree). That's a version-skew bug in Arduino's
 * glue layer, not a hardware or TinyUSB-stack limitation: esp_tinyusb's own
 * Kconfig explicitly supports ESP32-P4 (TINYUSB_RHPORT_HS defaults on for
 * IDF_TARGET_ESP32P4) and the vendored tinyusb port has a real DWC2 device
 * controller driver for it. This file calls esp_tinyusb/tinyusb directly,
 * bypassing Arduino's USB wrapper entirely -- same "native IDF code doing
 * the hardware bring-up, Arduino-side service calls into it" architecture
 * this project already uses for display/SIC/I2C.
 *
 * Device layout: 4 HID interfaces (keyboard/mouse/gamepad/system-control,
 * CONFIG_TINYUSB_HID_COUNT=4), one CDC-ACM interface (the plain USB-serial
 * CLI, driving the same Bus Pirate shell the touchscreen/webui share), and
 * one MSC interface backing Tab5's SD card. TinyUSB's composite descriptor
 * is fixed at driver-install time -- unlike
 * Arduino's per-feature lazy .begin(), there's no way to add a NEW USB
 * interface after the bus has enumerated, so usb_device_start() always
 * brings up the whole composite device in one shot (idempotent); which
 * logical feature is "active" is still tracked by UsbS3Service's existing
 * boolean flags, unchanged.
 *
 * MSC/SD card ownership: see sic_sd_sdmmc_release_for_usb()'s header
 * comment in lib/SIC/include/sic/storage/sd_sdmmc.h for the full handoff
 * design (the SD card can only have one FatFs mount at a time).
 * usb_msc_attach_sdcard() must be called (and succeed) BEFORE
 * usb_device_start() for the MSC interface to actually expose real media;
 * if no card is present, the MSC interface is still present in the
 * descriptor (CONFIG_TINYUSB_MSC_ENABLED is a build-time toggle) but has no
 * backing media registered until `usb stick` is run with a card inserted.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* product;
    const char* manufacturer;
    const char* serial;
    uint16_t vid;
    uint16_t pid;
} usb_device_ids_t;

// Records the device identity strings for the NEXT usb_device_start() call.
// TinyUSB bakes string descriptors in at driver-install time, so this must
// be called before the first usb_device_start() (matches IUsbS3Service's
// own configure()-before-begin() contract).
void usb_device_configure(const usb_device_ids_t* ids);

// Brings up the composite TinyUSB device (idempotent -- returns true
// immediately if already running). Caller must have released host mode
// first (same mutual-exclusivity convention UsbS3Service::usbHostBegin()
// already enforces via stopTinyUsbDevice()).
bool usb_device_start(void);

// Full teardown (tinyusb_driver_uninstall()) -- frees the USB-OTG HS port
// so usbHostBegin() can claim it, or so usb_device_start() can cleanly
// re-run later (e.g. after reconfigure()).
void usb_device_stop(void);
bool usb_device_is_running(void);

// ---- HID --------------------------------------------------------------
void usb_hid_keyboard_write_char(char c);
void usb_hid_keyboard_release_all(void);

void usb_hid_mouse_move(int8_t dx, int8_t dy);
void usb_hid_mouse_button(uint8_t button_mask, bool pressed); // bit0=left,1=right,2=middle

// hat: 0=up,1=up-right,2=right,...,7=up-left, 8=centered (matches HID hat-switch convention)
void usb_hid_gamepad_report(uint8_t hat, uint32_t buttons);

// usage: 0x81=Power Down, 0x82=Sleep, 0x83=Wake Up (Generic Desktop/System Control page)
void usb_hid_sysctrl_press(uint8_t usage);
void usb_hid_sysctrl_release(void);

// ---- CDC-ACM (plain USB-serial CLI) ---------------------------------------
// Decoupled from the GUI layer like the HID senders above -- this file only
// knows TinyUSB. Board-specific glue registers an RX callback and calls
// usb_cdc_write() for TX.

// Call once, early. Delivered from TinyUSB's own task (not an ISR), so the
// callback can do normal FreeRTOS calls (xQueueSend() etc.).
void usb_cdc_set_rx_callback(void (*cb)(const uint8_t* data, size_t len));

// No-op if not running or no host has the port open -- same silent-drop
// convention as a disconnected webui client.
void usb_cdc_write(const char* data, size_t len);

// True once a host has the CDC port open (DTR asserted).
bool usb_cdc_connected(void);

// ---- MSC (SD card) -------------------------------------------------------
// Takes exclusive ownership of the SD card from SIC's own FatFs mount (see
// this header's top comment). Call before usb_device_start(). Returns false
// if no card is present/initializable -- non-fatal, the composite device
// still comes up for the HID interfaces.
bool usb_msc_attach_sdcard(void);
// Releases the card back to SIC's normal lazy-mount path. Call after
// usb_device_stop().
void usb_msc_detach_sdcard(void);
bool usb_msc_sdcard_attached(void);

#ifdef __cplusplus
}
#endif
