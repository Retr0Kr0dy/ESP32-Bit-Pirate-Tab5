/*
 * usb_device.c — see usb_device.h for the full design rationale.
 *
 * Composite descriptor: esp_tinyusb only auto-generates a configuration
 * descriptor when NO class beyond CDC is enabled (confirmed by reading
 * descriptors_control.c's tinyusb_set_descriptors(): it hard-errors asking
 * for a caller-supplied configuration_descriptor whenever CFG_TUD_HID > 0).
 * So the composite HID x4 + MSC x1 configuration descriptor below is
 * hand-built from TinyUSB's own standard TUD_CONFIG_DESCRIPTOR/
 * TUD_HID_DESCRIPTOR/TUD_MSC_DESCRIPTOR template macros (device/usbd.h) --
 * this is the standard, documented way to do a multi-class TinyUSB
 * composite device, not a workaround.
 *
 * MSC class callbacks (tud_msc_*) are NOT defined here: esp_tinyusb's
 * tusb_msc_storage component (tusb_msc_storage.c) already implements all of
 * them internally once tinyusb_msc_storage_init_sdmmc() registers a card --
 * defining our own would collide.
 */
#include "usb_device.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "tusb_msc_storage.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#include "sic/storage/sd_sdmmc.h"

static const char* TAG = "usb_device";

// ---- Device/string descriptors --------------------------------------------
// tinyusb keeps POINTERS into these, not copies -- must outlive the device,
// hence static storage, populated once by usb_device_configure().
#define USB_STR_MAX 32
static char s_manufacturer[USB_STR_MAX] = "ESP32";
static char s_product[USB_STR_MAX]      = "Bit Pirate Tab5";
static char s_serial[USB_STR_MAX]       = "000000";
static const char s_langid[2]           = {0x09, 0x04}; // English (US), raw LANGID bytes per esp_tinyusb's own convention

static const char* s_string_desc[4];

// Required whenever a custom device_descriptor is supplied (confirmed by
// reading esp_tinyusb's tinyusb_set_descriptors(): it only auto-builds a
// qualifier when the device descriptor is esp_tinyusb's own Kconfig-driven
// default -- otherwise it hard-errors "Qualifier descriptor must be
// present"). Describes what the device would look like at the OTHER speed
// (full-speed, since bcdUSB above is high-speed-capable) -- same class/
// protocol/EP0 size as the real device descriptor, bNumConfigurations=1.
static const tusb_desc_device_qualifier_t s_qualifier_desc = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};

static tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00, // per-interface classes (no IAD needed -- every function below is single-interface)
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A, // Espressif default, overwritten by usb_device_configure()
    .idProduct          = 0x8888,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

// ---- HID report descriptors (one TinyUSB "instance" each, no Report ID
// multiplexing needed -- separate interfaces) -------------------------------
static const uint8_t s_hid_report_desc_keyboard[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static const uint8_t s_hid_report_desc_mouse[]    = { TUD_HID_REPORT_DESC_MOUSE() };
static const uint8_t s_hid_report_desc_gamepad[]  = { TUD_HID_REPORT_DESC_GAMEPAD() };

// Generic Desktop / System Control page (Usage 0x80), NOT the Consumer page
// TUD_HID_REPORT_DESC_CONSUMER() covers (that's media keys -- different HID
// usage page entirely) -- matches what Arduino's USBHIDSystemControl sent
// (Power Down/Sleep/Wake Up, usages 0x81-0x83). Three momentary-button bits
// (variable, not array-style) rather than a single-usage array report: more
// robust across host HID parsers for a 3-usage set.
static const uint8_t s_hid_report_desc_sysctrl[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x80,       // Usage (System Control)
    0xA1, 0x01,       // Collection (Application)
    0x19, 0x81,       //   Usage Minimum (System Power Down)
    0x29, 0x83,       //   Usage Maximum (System Wake Up)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x03,       //   Report Count (3)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x75, 0x05,       //   Report Size (5)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x03,       //   Input (Const,Var,Abs) -- pad to a full byte
    0xC0,             // End Collection
};

// ITF_CDC occupies TWO interface numbers (TUD_CDC_DESCRIPTOR's own Interface
// Association Descriptor bundles a control interface at _itfnum and a data
// interface at _itfnum+1) -- ITF_MSC/ITF_COUNT account for that gap.
enum { ITF_KEYBOARD = 0, ITF_MOUSE, ITF_GAMEPAD, ITF_SYSCTRL, ITF_CDC, ITF_MSC = ITF_CDC + 2, ITF_COUNT };

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
        case ITF_KEYBOARD: return s_hid_report_desc_keyboard;
        case ITF_MOUSE:    return s_hid_report_desc_mouse;
        case ITF_GAMEPAD:  return s_hid_report_desc_gamepad;
        case ITF_SYSCTRL:  return s_hid_report_desc_sysctrl;
        default:           return NULL;
    }
}

// No GET_REPORT/SET_REPORT feature use on any of these four (no LEDs/rumble,
// no host-initiated reads) -- trivial compliant stubs; TinyUSB's HID class
// driver requires these symbols to link regardless.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

// Diagnostics: tud_mount_cb()/tud_umount_cb() are already strongly defined
// by esp_tinyusb's own tusb_msc_storage.c (it needs them for its own mount-
// state bookkeeping) -- defining them here too is a link error (confirmed:
// "multiple definition of `tud_mount_cb'"), so use the standard tud_mounted()
// QUERY function from usb_hid_keyboard_write_char() instead of a callback.
// tud_hid_set_protocol_cb() has no such conflict -- confirms whether a host
// actually completed boot/report protocol negotiation on the keyboard/mouse
// interfaces specifically (hosts commonly send this during HID_SUBCLASS_BOOT
// enumeration), which the previous test run had no visibility into at all.
#if CFG_TUD_HID
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    ESP_LOGW(TAG, "tud_hid_set_protocol_cb: itf=%u protocol=%u (0=boot,1=report)", instance, protocol);
}
#endif

// ---- Configuration descriptors (full-speed + high-speed; ESP32-P4's
// OTG-HS port negotiates high-speed by default -- see tusb_config.h's
// TINYUSB_RHPORT_HS -- but TinyUSB's device stack still requires a
// same-length FS variant per descriptors_control.c's own check). -----------
//
// Two variants, chosen at usb_device_start() time: esp_tinyusb's MSC class
// callbacks (tusb_msc_storage.c) assume tinyusb_msc_storage_init_sdmmc()
// already registered real media BEFORE the host can query the interface --
// confirmed the hard way, via a real crash (NULL deref in
// tud_msc_test_unit_ready_cb) when a host queried an MSC interface that was
// advertised in the descriptor but never backed by usb_msc_attach_sdcard()
// (e.g. running "usb keyboard" without ever running "usb stick"/having a
// card present). TinyUSB enumerates interfaces strictly from the supplied
// descriptor bytes, so when no card is available, the fix is to not
// describe an MSC interface at all for that session -- the host then never
// knows to ask -- rather than try to make the class driver survive a query
// against unregistered media.
#define USB_HID_EP_SIZE   8
// CDC: notification EP6 + bulk EP7. P4's OTG-HS port allows 8 IN endpoints
// (dwc2_esp32.h's ep_in_count); this composite uses 7, one spare.
#define USB_CDC_NOTIF_EP_SIZE 8
#define CONFIG_TOTAL_LEN_MSC    (TUD_CONFIG_DESC_LEN + 4 * TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)
#define CONFIG_TOTAL_LEN_NO_MSC (TUD_CONFIG_DESC_LEN + 4 * TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
#define ITF_COUNT_NO_MSC (ITF_CDC + 2)

static const uint8_t s_fs_config_desc_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN_MSC, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_desc_keyboard), 0x81, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_MOUSE,    0, HID_ITF_PROTOCOL_MOUSE,    sizeof(s_hid_report_desc_mouse),    0x82, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_GAMEPAD,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_gamepad),  0x83, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_SYSCTRL,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_sysctrl),  0x84, USB_HID_EP_SIZE, 10),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 0, 0x86, USB_CDC_NOTIF_EP_SIZE, 0x07, 0x87, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 0, 0x05, 0x85, 64),
};

static const uint8_t s_hs_config_desc_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN_MSC, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_desc_keyboard), 0x81, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_MOUSE,    0, HID_ITF_PROTOCOL_MOUSE,    sizeof(s_hid_report_desc_mouse),    0x82, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_GAMEPAD,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_gamepad),  0x83, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_SYSCTRL,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_sysctrl),  0x84, USB_HID_EP_SIZE, 6),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 0, 0x86, USB_CDC_NOTIF_EP_SIZE, 0x07, 0x87, 512),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 0, 0x05, 0x85, 512),
};

static const uint8_t s_fs_config_desc_no_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT_NO_MSC, 0, CONFIG_TOTAL_LEN_NO_MSC, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_desc_keyboard), 0x81, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_MOUSE,    0, HID_ITF_PROTOCOL_MOUSE,    sizeof(s_hid_report_desc_mouse),    0x82, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_GAMEPAD,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_gamepad),  0x83, USB_HID_EP_SIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_SYSCTRL,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_sysctrl),  0x84, USB_HID_EP_SIZE, 10),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 0, 0x86, USB_CDC_NOTIF_EP_SIZE, 0x07, 0x87, 64),
};

static const uint8_t s_hs_config_desc_no_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT_NO_MSC, 0, CONFIG_TOTAL_LEN_NO_MSC, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_desc_keyboard), 0x81, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_MOUSE,    0, HID_ITF_PROTOCOL_MOUSE,    sizeof(s_hid_report_desc_mouse),    0x82, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_GAMEPAD,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_gamepad),  0x83, USB_HID_EP_SIZE, 6),
    TUD_HID_DESCRIPTOR(ITF_SYSCTRL,  0, HID_ITF_PROTOCOL_NONE,     sizeof(s_hid_report_desc_sysctrl),  0x84, USB_HID_EP_SIZE, 6),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 0, 0x86, USB_CDC_NOTIF_EP_SIZE, 0x07, 0x87, 512),
};

// ---- Lifecycle -------------------------------------------------------------
static bool s_running = false;

void usb_device_configure(const usb_device_ids_t* ids) {
    if (!ids) return;
    if (ids->manufacturer) { strncpy(s_manufacturer, ids->manufacturer, USB_STR_MAX - 1); s_manufacturer[USB_STR_MAX - 1] = 0; }
    if (ids->product)      { strncpy(s_product, ids->product, USB_STR_MAX - 1); s_product[USB_STR_MAX - 1] = 0; }
    if (ids->serial)       { strncpy(s_serial, ids->serial, USB_STR_MAX - 1); s_serial[USB_STR_MAX - 1] = 0; }
    if (ids->vid) s_device_desc.idVendor  = ids->vid;
    if (ids->pid) s_device_desc.idProduct = ids->pid;
}

bool usb_device_start(void) {
    if (s_running) return true;

    // Try to grab the SD card for MSC regardless of which specific feature
    // triggered this start (keyboard/mouse/gamepad/sysctrl/stick all funnel
    // through this one composite-device bring-up) -- the MSC-vs-no-MSC
    // descriptor choice below has to be made once, for the whole session,
    // before the driver installs. See the config descriptor block's header
    // comment for why this can't be "attach later if asked".
    bool haveMsc = usb_msc_attach_sdcard();

    s_string_desc[0] = s_langid;
    s_string_desc[1] = s_manufacturer;
    s_string_desc[2] = s_product;
    s_string_desc[3] = s_serial;

    tinyusb_config_t cfg = {
        .device_descriptor = &s_device_desc,
        .string_descriptor = s_string_desc,
        .string_descriptor_count = 4,
        .external_phy = false,
        .configuration_descriptor = haveMsc ? s_fs_config_desc_msc : s_fs_config_desc_no_msc,
        .hs_configuration_descriptor = haveMsc ? s_hs_config_desc_msc : s_hs_config_desc_no_msc,
        .qualifier_descriptor = &s_qualifier_desc,
        .self_powered = false,
        .vbus_monitor_io = -1,
    };

    if (!haveMsc) {
        ESP_LOGW(TAG, "no SD card available -- starting HID-only (no MSC interface this session)");
    }

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", (int)err);
        return false;
    }

    s_running = true;
    ESP_LOGI(TAG, "USB device started (HID keyboard/mouse/gamepad/sysctrl + CDC + MSC)");
    return true;
}

void usb_device_stop(void) {
    if (!s_running) return;
    tinyusb_driver_uninstall();
    s_running = false;
    // usb_device_start() always attempts the attach itself (see its own
    // comment) regardless of whether the caller ever asked for storage
    // specifically -- detach here unconditionally too, rather than relying
    // on the caller to have tracked that. usb_msc_detach_sdcard() is itself
    // a no-op if nothing is attached.
    usb_msc_detach_sdcard();
}

bool usb_device_is_running(void) { return s_running; }

// ---- HID senders ------------------------------------------------------------
// ASCII -> HID keycode/modifier: covers the printable range + a handful of
// control codes Bit Pirate's keyboardSendString()/keyboardSendChunkedString()
// actually push (newline, tab) -- same coverage Arduino's USBHIDKeyboard
// internally had via its own ASCII table (USBHIDKeyboard::write(char)).
static bool ascii_to_hid(char c, uint8_t* modifier, uint8_t* keycode) {
    *modifier = 0;
    if (c >= 'a' && c <= 'z') { *keycode = 0x04 + (c - 'a'); return true; }
    if (c >= 'A' && c <= 'Z') { *modifier = 0x02; *keycode = 0x04 + (c - 'A'); return true; } // left shift
    if (c >= '1' && c <= '9') { *keycode = 0x1E + (c - '1'); return true; }
    switch (c) {
        case '0': *keycode = 0x27; return true;
        case '\n': case '\r': *keycode = 0x28; return true; // Enter
        case '\t': *keycode = 0x2B; return true;
        case ' ':  *keycode = 0x2C; return true;
        case '-':  *keycode = 0x2D; return true;
        case '=':  *keycode = 0x2E; return true;
        case '[':  *keycode = 0x2F; return true;
        case ']':  *keycode = 0x30; return true;
        case '\\': *keycode = 0x31; return true;
        case ';':  *keycode = 0x33; return true;
        case '\'': *keycode = 0x34; return true;
        case '`':  *keycode = 0x35; return true;
        case ',':  *keycode = 0x36; return true;
        case '.':  *keycode = 0x37; return true;
        case '/':  *keycode = 0x38; return true;
        case '!':  *modifier = 0x02; *keycode = 0x1E; return true;
        case '@':  *modifier = 0x02; *keycode = 0x1F; return true;
        case '#':  *modifier = 0x02; *keycode = 0x20; return true;
        case '$':  *modifier = 0x02; *keycode = 0x21; return true;
        case '%':  *modifier = 0x02; *keycode = 0x22; return true;
        case '^':  *modifier = 0x02; *keycode = 0x23; return true;
        case '&':  *modifier = 0x02; *keycode = 0x24; return true;
        case '*':  *modifier = 0x02; *keycode = 0x25; return true;
        case '(':  *modifier = 0x02; *keycode = 0x26; return true;
        case ')':  *modifier = 0x02; *keycode = 0x27; return true;
        case '_':  *modifier = 0x02; *keycode = 0x2D; return true;
        case '+':  *modifier = 0x02; *keycode = 0x2E; return true;
        case '{':  *modifier = 0x02; *keycode = 0x2F; return true;
        case '}':  *modifier = 0x02; *keycode = 0x30; return true;
        case '|':  *modifier = 0x02; *keycode = 0x31; return true;
        case ':':  *modifier = 0x02; *keycode = 0x33; return true;
        case '"':  *modifier = 0x02; *keycode = 0x34; return true;
        case '~':  *modifier = 0x02; *keycode = 0x35; return true;
        case '<':  *modifier = 0x02; *keycode = 0x36; return true;
        case '>':  *modifier = 0x02; *keycode = 0x37; return true;
        case '?':  *modifier = 0x02; *keycode = 0x38; return true;
        default: return false;
    }
}

// Interrupt IN endpoints are hardware-paced (the host only polls them every
// bInterval -- a few ms at HS, up to 10ms at FS per our descriptors): a
// SINGLE immediate tud_hid_n_ready() check-and-drop, with no pacing between
// back-to-back sends, starves itself -- the first report in a burst finds
// the endpoint idle and succeeds, but every following one in a tight loop
// finds it still mid-transfer (not yet polled by the host) and gets
// silently dropped. Root-caused on real hardware: a 6-attempt test of a
// ~24-character string produced exactly ONE character per attempt, not a
// random subset -- consistent with "only the very first send of each burst
// ever finds the endpoint idle." Fix: actually WAIT (bounded) for
// tud_hid_n_ready() before each send instead of a one-shot check.
static bool waitHidReady(uint8_t itf, uint32_t timeoutMs) {
    for (uint32_t waited = 0; !tud_hid_n_ready(itf); waited += 2) {
        if (waited >= timeoutMs) return false;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

void usb_hid_keyboard_write_char(char c) {
    if (!s_running) return;
    if (!tud_mounted()) {
        ESP_LOGW(TAG, "keyboard: not mounted yet (host hasn't finished configuring) -- dropping '%c'", c);
        return;
    }
    uint8_t modifier = 0, keycode = 0;
    if (!ascii_to_hid(c, &modifier, &keycode)) return;

    bool ok = false, okRelease = false;
    if (waitHidReady(ITF_KEYBOARD, 100)) {
        uint8_t keys[6] = { keycode, 0, 0, 0, 0, 0 };
        ok = tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, modifier, keys);
    }
    // Brief hold then release -- a real key press, not a latch (matches
    // Arduino's USBHIDKeyboard::write(), which sends press+release per call).
    vTaskDelay(pdMS_TO_TICKS(8));
    if (waitHidReady(ITF_KEYBOARD, 100)) {
        okRelease = tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, (uint8_t[6]){0});
    }
    if (!ok || !okRelease) {
        ESP_LOGW(TAG, "keyboard: report send failed for '%c' (press=%d release=%d)", c, ok, okRelease);
    }
}

void usb_hid_keyboard_release_all(void) {
    if (!s_running) return;
    if (waitHidReady(ITF_KEYBOARD, 100)) {
        tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, (uint8_t[6]){0});
    }
}

void usb_hid_mouse_move(int8_t dx, int8_t dy) {
    if (!s_running || !waitHidReady(ITF_MOUSE, 100)) return;
    tud_hid_n_mouse_report(ITF_MOUSE, 0, 0, dx, dy, 0, 0);
}

void usb_hid_mouse_button(uint8_t button_mask, bool pressed) {
    if (!s_running) return;
    static uint8_t s_buttons = 0;
    s_buttons = pressed ? (s_buttons | button_mask) : (s_buttons & ~button_mask);
    if (!waitHidReady(ITF_MOUSE, 100)) return;
    tud_hid_n_mouse_report(ITF_MOUSE, 0, s_buttons, 0, 0, 0, 0);
}

void usb_hid_gamepad_report(uint8_t hat, uint32_t buttons) {
    if (!s_running || !waitHidReady(ITF_GAMEPAD, 100)) return;
    tud_hid_n_gamepad_report(ITF_GAMEPAD, 0, 0, 0, 0, 0, 0, 0, hat, buttons);
}

void usb_hid_sysctrl_press(uint8_t usage) {
    if (!s_running || !waitHidReady(ITF_SYSCTRL, 100)) return;
    uint8_t report = 0;
    if (usage == 0x81) report = 0x01;      // System Power Down
    else if (usage == 0x82) report = 0x02; // System Sleep
    else if (usage == 0x83) report = 0x04; // System Wake Up
    tud_hid_n_report(ITF_SYSCTRL, 0, &report, 1);
}

void usb_hid_sysctrl_release(void) {
    if (!s_running || !waitHidReady(ITF_SYSCTRL, 100)) return;
    uint8_t report = 0;
    tud_hid_n_report(ITF_SYSCTRL, 0, &report, 1);
}

// ---- CDC-ACM (plain USB-serial CLI) -----------------------------------------
static void (*s_cdc_rx_cb)(const uint8_t* data, size_t len) = NULL;

void usb_cdc_set_rx_callback(void (*cb)(const uint8_t* data, size_t len)) {
    s_cdc_rx_cb = cb;
}

void usb_cdc_write(const char* data, size_t len) {
    if (!s_running || !tud_cdc_connected() || len == 0) return;
    tud_cdc_write(data, len);
    tud_cdc_write_flush();
}

bool usb_cdc_connected(void) {
    return s_running && tud_cdc_connected();
}

// Drains whatever's currently buffered each time TinyUSB signals RX data.
void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    if (!s_cdc_rx_cb) return;
    uint8_t buf[64];
    uint32_t n;
    while ((n = tud_cdc_read(buf, sizeof(buf))) > 0) {
        s_cdc_rx_cb(buf, n);
    }
}

// ---- MSC (SD card handoff) -------------------------------------------------
// Owns its OWN temporary sdmmc host/card/LDO instance, entirely separate
// from SIC's -- see sd_sdmmc.h's sic_sd_sdmmc_release_for_usb() comment for
// why the two can't share one. Pins/LDO channel are read from SIC's last
// configured values (sic_sd_sdmmc_last_cfg()) so this stays correct if the
// board's SD pin assignment ever changes, rather than hardcoding Tab5's
// pins a second time.
static sdmmc_card_t* s_msc_card = NULL;
static sd_pwr_ctrl_handle_t s_msc_pwr = NULL;
static int s_msc_slot = -1;

bool usb_msc_sdcard_attached(void) { return s_msc_card != NULL; }

bool usb_msc_attach_sdcard(void) {
    if (s_msc_card) return true; // already attached

    const sic_sd_sdmmc_cfg_t* cfg = sic_sd_sdmmc_last_cfg();
    if (!cfg) {
        ESP_LOGW(TAG, "no SD card configured on this board -- MSC interface will have no media");
        return false;
    }

    sic_sd_sdmmc_release_for_usb(); // give up SIC's own mount first (see header comment)

    if (cfg->ldo_chan >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = cfg->ldo_chan };
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_msc_pwr) != ESP_OK) {
            ESP_LOGE(TAG, "SD LDO power-up failed");
            s_msc_pwr = NULL;
            return false;
        }
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.pwr_ctrl_handle = s_msc_pwr;
    host.slot = SDMMC_HOST_SLOT_0; // same instance SIC uses -- slot 1 is esp-hosted's (see sd_sdmmc_espidf.c's own comment)
    s_msc_slot = host.slot;

    if (sdmmc_host_init() != ESP_OK) goto fail;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)cfg->clk_pin;
    slot.cmd = (gpio_num_t)cfg->cmd_pin;
    slot.d0  = (gpio_num_t)cfg->d0_pin;
    slot.d1  = (gpio_num_t)cfg->d1_pin;
    slot.d2  = (gpio_num_t)cfg->d2_pin;
    slot.d3  = (gpio_num_t)cfg->d3_pin;
    slot.width = 4;
    if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) goto fail_host;

    s_msc_card = calloc(1, sizeof(sdmmc_card_t));
    if (!s_msc_card || sdmmc_card_init(&host, s_msc_card) != ESP_OK) goto fail_slot;

    tinyusb_msc_sdmmc_config_t msc_cfg = { .card = s_msc_card };
    if (tinyusb_msc_storage_init_sdmmc(&msc_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_storage_init_sdmmc failed");
        goto fail_card;
    }

    ESP_LOGI(TAG, "USB MSC: SD card attached");
    return true;

fail_card:
    free(s_msc_card); s_msc_card = NULL;
fail_slot:
    sdmmc_host_deinit_slot(s_msc_slot);
fail_host:
    // Deliberately NOT sdmmc_host_deinit() here -- see usb_msc_detach_sdcard()'s
    // own comment just below for why that's a real, confirmed-on-hardware hazard.
fail:
    if (s_msc_pwr) { sd_pwr_ctrl_del_on_chip_ldo(s_msc_pwr); s_msc_pwr = NULL; }
    s_msc_slot = -1;
    return false;
}

void usb_msc_detach_sdcard(void) {
    if (!s_msc_card) return;
    tinyusb_msc_storage_deinit();
    // sdmmc_host_deinit_slot() only -- NOT sdmmc_host_deinit(). The latter's
    // own doc comment says it "will deinitialize the host immediately,
    // regardless of the number of active slots": sdmmc_host_init()/_deinit()
    // are global to the WHOLE SDMMC peripheral (both slots), while only
    // sdmmc_host_init_slot()/_deinit_slot() are actually per-slot (confirmed
    // against SDMMC_HOST_DEFAULT()'s own .deinit_p = &sdmmc_host_deinit_slot,
    // which is what esp_vfs_fat_sdcard_unmount() -- SIC's own, known-safe
    // unmount path -- calls). esp-hosted's WiFi/BLE link also runs over
    // SDMMC (slot 1, see sd_sdmmc_espidf.c's own comment on why the SD card
    // uses slot 0 instead) and shares this SAME global host driver state --
    // calling the global deinit here tore it down from under esp-hosted,
    // confirmed on real hardware: it immediately broke the C6 link
    // ("H_SDIO_DRV: failed to read registers") and force-restarted the whole
    // board. The global host driver is cheap to just leave initialized
    // (sdmmc_host_init() is idempotent -- "ESP_OK ... if already
    // initialized" -- so the next usb_msc_attach_sdcard() call re-using it
    // costs nothing extra).
    sdmmc_host_deinit_slot(s_msc_slot);
    if (s_msc_pwr) { sd_pwr_ctrl_del_on_chip_ldo(s_msc_pwr); s_msc_pwr = NULL; }
    free(s_msc_card);
    s_msc_card = NULL;
    s_msc_slot = -1;
    // SIC's own g_ctx.mounted is already 0 (release_for_usb() cleared it) --
    // the next normal SD access (file browser, etc.) remounts from scratch
    // via sd_sdmmc_begin()'s existing lazy path, same as a fresh card swap.
}
