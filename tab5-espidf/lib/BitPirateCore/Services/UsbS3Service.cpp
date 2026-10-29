#include "UsbS3Service.h"
#include <sstream>
#include <algorithm>
#include <esp_mac.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Gamepad hat/button encoding for usb_hid_gamepad_report() -- standard
// 8-direction hat switch (0=up,2=right,4=down,6=left,8=centered, see
// TUD_HID_REPORT_DESC_GAMEPAD()'s own convention) and an arbitrary-but-fixed
// bit per named button (this is a new native report shape, nothing else
// depends on matching Arduino's old USBHIDGamepad bit positions).
namespace {
    constexpr uint8_t kHatUp = 0, kHatRight = 2, kHatDown = 4, kHatLeft = 6, kHatCenter = 8;
    constexpr uint32_t kBtnA = 1u << 0, kBtnB = 1u << 1, kBtnStart = 1u << 2, kBtnSelect = 1u << 3;
}

UsbS3Service::UsbS3Service()
  : keyboardActive(false), storageActive(false) {}

void UsbS3Service::configure(const char* productStr, const char* manufacturerStr, const char* serialStr, uint16_t vid, uint16_t pid, const char* /*webUSBString*/) {
    // webUSBURL has no equivalent in the native bridge (no WebUSB class
    // driver wired up) -- dropped, nothing in this project's command set
    // ever used it for anything beyond the (also-broken) Arduino USB.h path.
    usb_device_ids_t ids = { productStr, manufacturerStr, serialStr, vid, pid };
    usb_device_configure(&ids);
}

void UsbS3Service::keyboardBegin() {
    if (keyboardActive) return;
    usb_device_start();
    hidInitTime = millis();
    keyboardActive = true;
}

void UsbS3Service::keyboardSendString(const std::string& text) {
    if (!keyboardActive) return;

    // Wait for host enumeration to finish -- same settle window the old
    // Arduino path used, still needed: the host won't accept HID reports
    // until it's finished configuring the device after connect.
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }

    usb_hid_keyboard_release_all();
    for (char c : text) {
        usb_hid_keyboard_write_char(c);
    }
}

void UsbS3Service::keyboardSendChunkedString(const std::string& data, size_t chunkSize, unsigned long delayBetweenChunks) {
    if (!keyboardActive) return;

    size_t totalLength = data.length();
    size_t sentLength = 0;

    while (sentLength < totalLength) {
        size_t currentChunkSize = std::min(chunkSize, totalLength - sentLength);
        std::string chunk = data.substr(sentLength, currentChunkSize);
        keyboardSendString(chunk);
        sentLength += currentChunkSize;
        delay(delayBetweenChunks);
    }
}

void UsbS3Service::mouseBegin() {
    if (mouseActive) return;
    usb_device_start();
    mouseActive = true;
    hidInitTime = millis();
}

void UsbS3Service::mouseMove(int x, int y) {
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }
    // Native report is signed-8-bit-per-axis (one USB report = one relative
    // delta); clamp rather than silently wrap on a bigger requested move.
    usb_hid_mouse_move((int8_t)std::max(-127, std::min(127, x)),
                       (int8_t)std::max(-127, std::min(127, y)));
}

void UsbS3Service::mouseClick(int button) {
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }
    usb_hid_mouse_button((uint8_t)button, true);
    delay(50);
    usb_hid_mouse_button((uint8_t)button, false);
}

void UsbS3Service::mouseRelease(int button) {
    if (!mouseActive) return;
    usb_hid_mouse_button((uint8_t)button, false);
}

void UsbS3Service::gamepadBegin() {
    if (gamepadActive) return;
    usb_device_start();
    gamepadActive = true;
    hidInitTime = millis();
}

void UsbS3Service::gamepadPress(const std::string& name) {
    if (!gamepadActive) return;

    while (millis() - hidInitTime < 1500) {
        delay(10);
    }

    uint8_t hat = kHatCenter;
    uint32_t buttons = 0;

    if (name == "up") hat = kHatUp;
    else if (name == "down") hat = kHatDown;
    else if (name == "left") hat = kHatLeft;
    else if (name == "right") hat = kHatRight;
    else if (name == "a") buttons = kBtnA;
    else if (name == "b") buttons = kBtnB;
    else if (name == "start") buttons = kBtnStart;
    else if (name == "select") buttons = kBtnSelect;
    else return; // unknown

    usb_hid_gamepad_report(hat, buttons);
    delay(500);  // simulate short press
    usb_hid_gamepad_report(kHatCenter, 0);
}

bool UsbS3Service::isKeyboardActive() const {
    return keyboardActive;
}

bool UsbS3Service::isStorageActive() const {
    return storageActive;
}

bool UsbS3Service::isMouseActive() const {
    return mouseActive;
}

bool UsbS3Service::isGamepadActive() const {
    return gamepadActive;
}

bool UsbS3Service::isHostActive() const {
    return hostInstalled;
}

void UsbS3Service::storageBegin(uint8_t /*cs*/, uint8_t /*clk*/, uint8_t /*miso*/, uint8_t /*mosi*/) {
    // Parameters unused on this board: IUsbS3Service's signature was
    // designed for the S3's SPI-attached SD card; Tab5's SD card is wired
    // via a 4-bit SDMMC bus through SIC instead (see sic_sd_sdmmc_last_cfg()
    // in lib/SIC/include/sic/storage/sd_sdmmc.h), so main/usb_device.c's
    // usb_msc_attach_sdcard() gets its pins from the board profile rather
    // than from this call's arguments.
    sdUsbEnable();
}

void UsbS3Service::sdUsbEnable() {
    if (usb_msc_get_opt_in() && usb_device_is_running()) {
        // Already opted in and the composite device is up -- nothing to do.
        storageActive = usb_msc_sdcard_attached();
        return;
    }

    usb_msc_set_opt_in(true);

    // The MSC-vs-no-MSC choice is baked into the composite descriptor at
    // tinyusb_driver_install() time (see usb_device.h's header comment), so
    // if the device is already running without MSC (e.g. "usb keyboard" ran
    // first), it has to be torn down and brought back up for the new opt-in
    // to actually add the interface.
    if (usb_device_is_running()) {
        usb_device_stop();
    }

    usb_device_start();
    storageActive = usb_msc_sdcard_attached();
}

void UsbS3Service::sdUsbDisable() {
    usb_msc_set_opt_in(false);

    // Always bring the composite device back up after this, even with no
    // other HID feature active: its CDC interface is the plain USB-serial
    // CLI (confirmed live from boot, before any "usb ..." command ever
    // runs -- see main/boards/tab5_bitpirate/BitPirateTask.cpp's own
    // unconditional usb_device_start() call), not an optional feature this
    // service tracks a flag for. Leaving it stopped here would silently cut
    // the user's own USB-serial session -- the one they just used to type
    // "sdusb off" -- with nothing to bring it back except a manual reboot.
    if (usb_device_is_running()) {
        usb_device_stop();
        usb_device_start();
    }

    storageActive = usb_msc_sdcard_attached();
}

bool UsbS3Service::sdUsbIsEnabled() const {
    return usb_msc_get_opt_in();
}

bool UsbS3Service::stopTinyUsbDevice() {
    usb_device_stop(); // also detaches the SD card, if attached -- see its own comment
    storageActive = false;
    return true;
}

void UsbS3Service::hostClientEventCb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    auto* self = static_cast<UsbS3Service*>(arg);
    if (!self || !event_msg) return;

    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            self->devAddr = event_msg->new_dev.address;
            self->attachPending = true;
            self->detachPending = false;
            self->dumpedThisAttach = false;
            break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            // device gone
            self->detachPending = true;
            self->attachPending = false;
            self->dumpedThisAttach = false;
            self->devAddr = 0;
            break;

        default:
            break;
    }
}

bool UsbS3Service::usbHostBegin() {
    if (hostInstalled) return true;

    // The USB port must be freed from the device mode before starting host
    stopTinyUsbDevice();

    usb_host_config_t host_cfg = {};
    host_cfg.skip_phy_setup = false;
    host_cfg.root_port_unpowered = false; 
    host_cfg.intr_flags = 0;
    host_cfg.enum_filter_cb = nullptr;
    host_cfg.fifo_settings_custom = {0, 0, 0}; 
    host_cfg.peripheral_map = 0;

    esp_err_t err = usb_host_install(&host_cfg);
    if (err != ESP_OK) {
        Serial.printf("usb_host_install failed: %d", (int)err);
        return false;
    }

    usb_host_client_config_t client_cfg = {};
    client_cfg.is_synchronous = false; 
    client_cfg.max_num_event_msg = 8;
    client_cfg.async.client_event_callback = &UsbS3Service::hostClientEventCb;
    client_cfg.async.callback_arg = this;

    err = usb_host_client_register(&client_cfg, &hostClient);
    if (err != ESP_OK) {
        Serial.printf("usb_host_client_register failed: %d", (int)err);
        usb_host_uninstall();
        return false;
    }

    // Reset state
    devAddr = 0;
    attachPending = false;
    detachPending = false;
    dumpedThisAttach = false;

    hostInstalled = true;
    return true;
}

void UsbS3Service::usbHostEnd() {
    if (!hostInstalled) return;

    if (hostClient) {
        usb_host_client_unblock(hostClient);
    }

    // free devices
    esp_err_t err = usb_host_device_free_all();
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        ESP_LOGW(TAG_USBHOST, "usb_host_device_free_all err=%d", (int)err);
    }

    // wait for all events to be processed
    uint32_t flags = 0;
    for (int i = 0; i < 50; ++i) {
        flags = 0;
        esp_err_t e2 = usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        (void)e2;
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) break;
    }

    // unregister client and uninstall host
    if (hostClient) {
        usb_host_client_deregister(hostClient);
        hostClient = nullptr;
    }

    usb_host_uninstall();

    hostInstalled = false;
    devAddr = 0;
    attachPending = false;
    detachPending = false;
    dumpedThisAttach = false;
}

bool UsbS3Service::isSystemControlActive() const {
    return systemControlActive;
}

void UsbS3Service::systemControlBegin() {
    if (systemControlActive) return;
    usb_device_start();
    systemControlActive = true;
    hidInitTime = millis();
}

void UsbS3Service::systemControlEnd() {
    if (!systemControlActive) return;
    usb_hid_sysctrl_release();
    systemControlActive = false;
}

void UsbS3Service::systemSleep() {
    if (!systemControlActive) systemControlBegin();
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }
    usb_hid_sysctrl_press(0x82); // System Sleep
    usb_hid_sysctrl_release();
}

void UsbS3Service::systemWake() {
    if (!systemControlActive) systemControlBegin();
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }
    usb_hid_sysctrl_press(0x83); // System Wake Up
    usb_hid_sysctrl_release();
}

void UsbS3Service::systemPowerOff(uint32_t holdMs) {
    if (!systemControlActive) systemControlBegin();
    while (millis() - hidInitTime < 1500) {
        delay(10);
    }
    usb_hid_sysctrl_press(0x81); // System Power Down

    if (holdMs > 0) {
        delay(holdMs); // simulate long press
    }

    usb_hid_sysctrl_release();
}

void UsbS3Service::reset() {
    // Previously gated on "initialized", a flag only storageBegin() ever
    // set -- a keyboard/mouse/gamepad-only session (the common case) left
    // it false forever, so "usb reset" silently did nothing: the device
    // stayed up and every *Active flag stayed true. usb_hid_*_release() are
    // self-contained no-ops when the device isn't running, so there's
    // nothing to gate on those.
    usb_hid_keyboard_release_all();
    usb_hid_sysctrl_release();

    // Earlier version of this fix called usb_msc_detach_sdcard() directly
    // here. That's wrong whenever MSC is actually live: the MSC-vs-no-MSC
    // choice is baked into the enumerated USB descriptor at
    // tinyusb_driver_install() time (see usb_device.h's header comment), so
    // detaching the backing card without a full stop+restart leaves the
    // interface still advertised to the host while its backing storage is
    // already torn down underneath it -- the same class of hazard
    // tud_umount_cb()'s NULL-deref fix exists for, just in the opposite
    // direction (a host that still thinks it has a mounted drive, pointed
    // at freed storage, instead of a benign disconnect callback on
    // never-attached storage). sdUsbDisable() already does the correct
    // stop+restart cycle for exactly this transition, and leaves the CDC
    // interface (the plain USB-serial CLI the user is presumably typing
    // "reset" through right now) running afterward either way -- safe to
    // call unconditionally even when MSC was never opted in.
    sdUsbDisable();

    keyboardActive = false;
    mouseActive = false;
    systemControlActive = false;
    gamepadActive = false;
    hostInstalled = false;
}

// TODO: Rework this func to something more manageable
// Extract the parsing from this function to another obj
std::string UsbS3Service::usbHostTick() {
    if (!hostInstalled || !hostClient) {
        return "[USB][ERR] Host not started";
    }

    // Run host library events 
    uint32_t lib_flags = 0;
    esp_err_t err = usb_host_lib_handle_events(0, &lib_flags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return "[USB][ERR] usb_host_lib_handle_events err=" + std::to_string((int)err);
    }

    // Run client events
    err = usb_host_client_handle_events(hostClient, 0);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return "[USB][ERR] usb_host_client_handle_events err=" + std::to_string((int)err);
    }

    //  Messages one-shot
    if (detachPending) {
        detachPending = false;
        return "[USB] Device gone.";
    }

    if (attachPending) {
        attachPending = false;
        if (devAddr) {
            return "[USB] Device attached (addr=" + std::to_string(devAddr) + ").";
        }
    }

    // Dump once per attach
    if (devAddr != 0 && !dumpedThisAttach) {
        usb_device_handle_t devHandle = nullptr;

        err = usb_host_device_open(hostClient, devAddr, &devHandle);
        if (err != ESP_OK) {
            return "[USB][ERR] usb_host_device_open addr=" + std::to_string(devAddr) + " err=" + std::to_string((int)err);
        }

        // Device info (often gives extra useful stuff like speed / EP0 size)
        usb_device_info_t dinfo = {};
        esp_err_t infoErr = usb_host_device_info(devHandle, &dinfo);

        const usb_device_desc_t* dev_desc = nullptr;
        err = usb_host_get_device_descriptor(devHandle, &dev_desc);
        if (err != ESP_OK || !dev_desc) {
            usb_host_device_close(hostClient, devHandle);
            return "[USB][ERR] usb_host_get_device_descriptor err=" + std::to_string((int)err);
        }

        const usb_config_desc_t* cfg_desc = nullptr;
        err = usb_host_get_active_config_descriptor(devHandle, &cfg_desc);
        if (err != ESP_OK || !cfg_desc) {
            usb_host_device_close(hostClient, devHandle);
            return "[USB][ERR] usb_host_get_active_config_descriptor err=" + std::to_string((int)err);
        }

        auto hex8 = [](uint8_t v) {
            const char* h = "0123456789abcdef";
            std::string s = "0x00";
            s[2] = h[(v >> 4) & 0x0F];
            s[3] = h[v & 0x0F];
            return s;
        };
        auto hex16 = [](uint16_t v) {
            const char* h = "0123456789abcdef";
            std::string s = "0x0000";
            s[2] = h[(v >> 12) & 0x0F];
            s[3] = h[(v >> 8) & 0x0F];
            s[4] = h[(v >> 4) & 0x0F];
            s[5] = h[v & 0x0F];
            return s;
        };
        auto epTypeStr = [](uint8_t bmAttributes) {
            switch (bmAttributes & 0x03) {
                case 0x00: return "CTRL";
                case 0x01: return "ISOC";
                case 0x02: return "BULK";
                case 0x03: return "INTR";
                default:   return "?";
            }
        };

        std::ostringstream ss;
        ss << "\n===== USB Device (addr " << (int)devAddr << ") =====\n";

        // Device descriptor
        ss << "VID:PID " << hex16(dev_desc->idVendor) << ":" << hex16(dev_desc->idProduct) << "\n";
        ss << "bcdUSB " << ((dev_desc->bcdUSB >> 8) & 0xFF) << "." << (dev_desc->bcdUSB & 0xFF) << "\n";
        ss << "bMaxPacketSize0 " << (int)dev_desc->bMaxPacketSize0 << "\n";
        ss << "Device Class/Sub/Proto "
        << (int)dev_desc->bDeviceClass << "/"
        << (int)dev_desc->bDeviceSubClass << "/"
        << (int)dev_desc->bDeviceProtocol << "\n";
        ss << "bcdDevice " << hex16(dev_desc->bcdDevice) << "\n";
        ss << "iManufacturer=" << (int)dev_desc->iManufacturer
        << " iProduct=" << (int)dev_desc->iProduct
        << " iSerial=" << (int)dev_desc->iSerialNumber << "\n";
        ss << "NumConfigurations " << (int)dev_desc->bNumConfigurations << "\n";

        // Device info (if available)
        ss << "\n[DeviceInfo]\n";
        if (infoErr == ESP_OK) {
            ss << "address=" << (int)dinfo.dev_addr << "\n";
            ss << "speed=" << (int)dinfo.speed << " (enum)\n";
            ss << "mps0=" << (int)dinfo.bMaxPacketSize0 << "\n";
            ss << "config_value=" << (int)dinfo.bConfigurationValue << "\n";
        } else {
            ss << "usb_host_device_info() err=" << (int)infoErr << "\n";
        }

        // Config header
        ss << "\n[Configuration]\n";
        ss << "bConfigurationValue=" << (int)cfg_desc->bConfigurationValue
        << " bNumInterfaces=" << (int)cfg_desc->bNumInterfaces
        << " wTotalLength=" << (int)cfg_desc->wTotalLength << "\n";
        ss << "bmAttributes=" << hex8(cfg_desc->bmAttributes)
        << " bMaxPower=" << (int)cfg_desc->bMaxPower << " (2mA units)\n";

        // Parse descriptors inside config
        ss << "\n[Descriptors]\n";

        const uint8_t* p = (const uint8_t*)cfg_desc;
        const uint8_t* end = p + cfg_desc->wTotalLength;

        bool hasHid = false;
        bool hasInIntr = false;
        bool hasOutIntr = false;
        uint8_t currentIfClass = 0xFF;
        uint8_t ifSub;
        uint8_t ifProto;
        int ifCount = 0;
        int epCount = 0;

        while (p + 2 <= end) {
            uint8_t len = p[0];
            uint8_t type = p[1];
            if (len < 2) break;
            if (p + len > end) break;

            if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                const usb_intf_desc_t* ifd = (const usb_intf_desc_t*)p;
                ifCount++;
                ifSub   = ifd->bInterfaceSubClass;
                ifProto = ifd->bInterfaceProtocol;
                currentIfClass = ifd->bInterfaceClass;
                if (ifd->bInterfaceClass == 3) hasHid = true;

                ss << "IF " << (int)ifd->bInterfaceNumber
                << " alt " << (int)ifd->bAlternateSetting
                << " class/sub/proto "
                << (int)ifd->bInterfaceClass << "/"
                << (int)ifd->bInterfaceSubClass << "/"
                << (int)ifd->bInterfaceProtocol
                << " eps=" << (int)ifd->bNumEndpoints
                << " iInterface=" << (int)ifd->iInterface
                << "\n";
            }
            else if (type == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                const usb_ep_desc_t* ep = (const usb_ep_desc_t*)p;
                epCount++;

                bool in = (ep->bEndpointAddress & 0x80) != 0;
                uint8_t epNum = ep->bEndpointAddress & 0x0F;
                bool intr = ((ep->bmAttributes & 0x03) == 0x03);

                if (currentIfClass == 3 && intr && in) hasInIntr = true;
                if (currentIfClass == 3 && intr && !in) hasOutIntr = true;

                ss << "  EP " << (in ? "IN " : "OUT") << (int)epNum
                << " addr=" << hex8(ep->bEndpointAddress)
                << " type=" << epTypeStr(ep->bmAttributes)
                << " bmAttr=" << hex8(ep->bmAttributes)
                << " maxPacket=" << (int)ep->wMaxPacketSize
                << " interval=" << (int)ep->bInterval
                << "\n";
            }
            else if (type == 0x0B /* IAD */) {
                // Interface Association Descriptor (composite devices)
                ss << "IAD len=" << (int)len;
                if (len >= 8) {
                    uint8_t firstIf = p[2];
                    uint8_t ifCount = p[3];
                    uint8_t cls = p[4], sub = p[5], proto = p[6];
                    uint8_t iFunction = p[7];
                    ss << " firstIf=" << (int)firstIf
                    << " count=" << (int)ifCount
                    << " class/sub/proto " << (int)cls << "/" << (int)sub << "/" << (int)proto
                    << " iFunction=" << (int)iFunction;
                }
                ss << "\n";
            }
            else if (type == 0x21 /* HID */) {
                // HID descriptor: bcdHID,country,numDesc, (type,len)...
                ss << "  HID";
                if (len >= 9) {
                    uint16_t bcdHID = p[2] | (p[3] << 8);
                    uint8_t country = p[4];
                    uint8_t numDesc = p[5];
                    uint8_t descType = p[6];
                    uint16_t descLen = p[7] | (p[8] << 8);
                    ss << " bcdHID=" << hex16(bcdHID)
                    << " country=" << (int)country
                    << " numDesc=" << (int)numDesc
                    << " firstDescType=" << hex8(descType)
                    << " firstDescLen=" << (int)descLen;
                } else {
                    ss << " len=" << (int)len;
                }
                ss << "\n";
            }
            else if (type == 0x24 /* CS_INTERFACE */) {
                ss << "  CS_INTERFACE len=" << (int)len << "\n";
            }
            else if (type == 0x25 /* CS_ENDPOINT */) {
                ss << "  CS_ENDPOINT len=" << (int)len << "\n";
            }
            else {
                // For completeness, show unknown types briefly
                ss << "DESC type=" << hex8(type) << " len=" << (int)len << "\n";
            }

            p += len;
        }

        ss << "\n[Quick analysis]\n";
        ss << "- Interfaces_seen = " << ifCount << " Endpoints_seen = " << epCount << "\n";
        if (currentIfClass == 8) {
            ss << "- USB Mass Storage (usb drive likely)\n";
        }
        else if (currentIfClass == 1) {
            ss << "- USB Audio interface (headset/mic/speaker likely)\n";
        }
        else if (currentIfClass == 2 || currentIfClass == 10) {
            ss << "- USB CDC interface (serial device)\n";
        }
        else if (currentIfClass == 14) {
            ss << "- USB Video interface (webcam/capture device)\n";
        }
        
        if (hasHid && hasInIntr) {
            ss << "- HID interface, likely input device (gamepad/keyboard/mouse)\n";
            ss << "- OUT interrupt endpoint: " << (hasOutIntr ? "yes (rumble/LEDs possible)" : "no (maybe via control transfers)") << "\n";
        } else if (hasHid) {
            ss << "- HID interface but no IN interrupt endpoint found (unusual)\n";
        }

        ss << "===================================\n";

        usb_host_device_close(hostClient, devHandle);

        dumpedThisAttach = true;
        return ss.str();
    }

    return "";
}

std::string UsbS3Service::getUsbSerialFromEfuseMac() {
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return std::string(); // empty -> caller can fallback
    }

    char macSuffix[13] = {0}; // 12 hex + null
    snprintf(macSuffix, sizeof(macSuffix), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return std::string("ESP32-BP-") + macSuffix;
}