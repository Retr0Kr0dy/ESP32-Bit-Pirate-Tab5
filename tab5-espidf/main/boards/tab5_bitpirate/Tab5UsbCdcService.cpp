#include "Tab5UsbCdcService.h"

#include <cstring>
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "iot_usbh_cdc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "boards/tab5/ioexpander.h"  /* tab5_ioexp_set() -- USB5V_EN, see begin()/stop() */

namespace {

constexpr size_t RX_BUF = 4096;
constexpr size_t TX_BUF = 4096;
constexpr const char* TAG = "Tab5UsbCdc";

usbh_cdc_handle_t s_cdc = nullptr;
bool          s_hostInstalled = false;
TaskHandle_t  s_libTask = nullptr;
volatile int  s_preferredItf = -1;
volatile bool s_preferredValid = false;
volatile uint16_t s_vid = 0;
volatile bool s_allFreed = false;  // set by usbLibTask once the host lib
                                    // confirms every device is freed; stop()
                                    // polls this before usb_host_uninstall(),
                                    // which hard-requires it (see its own
                                    // doc comment in usb/usb_host.h).

// IO-expander 1 (0x44), bit 3 -- USB5V_EN, the USB-A port's VBUS-sourcing
// load switch (M5 schematic: gates U27/MT9700, output feeds J10 VCC
// directly). ioexpander.c's boot sequence now leaves this off by default
// (this fork runs USB-A as a TinyUSB *device* for the native composite,
// which must never source VBUS onto a host's own VBUS pin -- see
// EXP1_BOOT_OUT_SET's own comment for the full reasoning and the real
// hardware incident that prompted it). This service is the one legitimate
// exception: it's the actual *host*-mode user of this port, so it owns
// explicitly enabling the rail itself rather than assuming any particular
// boot default -- correct regardless of what else changes that default in
// the future.
constexpr int   USB5V_EN_EXPANDER = 1;
constexpr int   USB5V_EN_BIT      = 3;

// CP210x (Silicon Labs USB-UART bridge) vendor requests -- some C5 devkits
// carry a CP2102 instead of the native USB-Serial-JTAG; it must be enabled
// and configured, or no data flows.
constexpr uint16_t CP210X_VID          = 0x10C4;
constexpr uint8_t  CP210X_REQTYPE      = 0x41;   // vendor, host-to-device, interface
constexpr uint8_t  CP210X_IFC_ENABLE   = 0x00;
constexpr uint8_t  CP210X_SET_LINE_CTL = 0x03;
constexpr uint8_t  CP210X_SET_MHS      = 0x07;
constexpr uint8_t  CP210X_SET_BAUDRATE = 0x1E;
constexpr uint16_t CP210X_UART_ENABLE  = 0x0001;
constexpr uint16_t CP210X_LINE_8N1     = 0x0800;  // 8 data bits, no parity, 1 stop
constexpr uint16_t CP210X_MHS          = 0x0303;  // WRITE_DTR|WRITE_RTS|DTR|RTS

void cp210xInit(usbh_cdc_handle_t h, uint16_t itf) {
    usbh_cdc_send_custom_request(h, CP210X_REQTYPE, CP210X_IFC_ENABLE, CP210X_UART_ENABLE, itf, 0, nullptr);
    usbh_cdc_send_custom_request(h, CP210X_REQTYPE, CP210X_SET_LINE_CTL, CP210X_LINE_8N1, itf, 0, nullptr);
    uint32_t b = 115200;
    uint8_t baud[4] = { (uint8_t)(b & 0xFF), (uint8_t)((b >> 8) & 0xFF),
                        (uint8_t)((b >> 16) & 0xFF), (uint8_t)((b >> 24) & 0xFF) };
    usbh_cdc_send_custom_request(h, CP210X_REQTYPE, CP210X_SET_BAUDRATE, 0, itf, 4, baud);
    usbh_cdc_send_custom_request(h, CP210X_REQTYPE, CP210X_SET_MHS, CP210X_MHS, itf, 0, nullptr);
}

// Pumps the base USB host library events (device connect/disconnect, transfers).
void usbLibTask(void*) {
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            s_allFreed = true;
        }
    }
}

// On attach, pick the bulk-data interface. A native USB-Serial-JTAG C5
// exposes CDC comms on interface 0 and CDC data on interface 1; a
// CP2102-bridge C5 is vendor-class with its bulk endpoints on interface 0
// (no CDC_DATA descriptor).
void newDevCb(usb_device_handle_t dev, void*) {
    const usb_device_desc_t* dd = nullptr;
    usb_host_get_device_descriptor(dev, &dd);
    s_vid = dd ? dd->idVendor : 0;

    const usb_config_desc_t* cfg = nullptr;
    if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK || !cfg) return;

    int offset = 0;
    const usb_standard_desc_t* d = reinterpret_cast<const usb_standard_desc_t*>(cfg);
    int found = -1;
    while ((d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength,
                                                  USB_B_DESCRIPTOR_TYPE_INTERFACE, &offset))) {
        const usb_intf_desc_t* itf = reinterpret_cast<const usb_intf_desc_t*>(d);
        if (itf->bInterfaceClass == USB_CLASS_CDC_DATA) {
            found = itf->bInterfaceNumber;
            break;
        }
    }
    // CDC_DATA present -> use it (USB-Serial-JTAG). Otherwise the device is
    // a vendor-class bridge (CP210x) whose data endpoints live on interface 0.
    s_preferredItf = (found >= 0) ? found : 0;
    s_preferredValid = true;
}

}  // namespace

bool Tab5UsbCdcService::begin() {
    if (started && s_cdc) return true;

    // This service is a real USB *host* -- the one case on this board that
    // actually needs to source VBUS to power a downstream peripheral (a
    // C5/CP210x devkit). USB5V_EN is off by default now (see its own
    // comment in ioexpander.c), so this is no longer optional: without it,
    // a plugged-in peripheral simply never powers up, and everything below
    // silently times out waiting for a device that was never going to
    // appear. Owning this explicitly here, rather than trusting whatever
    // the board-wide boot default happens to be, is also what makes this
    // correct if that default ever changes again for an unrelated reason.
    if (tab5_ioexp_set(USB5V_EN_EXPANDER, USB5V_EN_BIT, 1) != 0) {
        ESP_LOGE(TAG, "failed to enable USB5V_EN -- downstream device won't be powered");
        return false;
    }

    // Every failure return from here on routes through stop() rather than
    // a bare `return false`, so begin() is transactional: on any failure,
    // whatever was partially acquired gets released and USB5V_EN goes back
    // off, instead of leaving VBUS sourced indefinitely with no host stack
    // actually using it -- exactly the kind of leftover hazardous state
    // the USB5V_EN default fix above exists to prevent in the first place.
    // stop() already handles every intermediate state correctly (safe to
    // call whether or not s_hostInstalled/s_cdc/the CDC driver ended up
    // set) -- see its own comments.

    if (!s_hostInstalled) {
        usb_host_config_t hostCfg = {};
        hostCfg.skip_phy_setup = false;
        hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
        esp_err_t err = usb_host_install(&hostCfg);
        // s_hostInstalled is false here, so ESP_ERR_INVALID_STATE cannot mean
        // "we already installed it" -- it means some other subsystem (e.g.
        // the TinyUSB device stack) already holds the PHY/controller this
        // call targeted. Any non-ESP_OK result is a hard failure: do not set
        // s_hostInstalled or spin up usbLibTask against a host library that
        // was never actually installed, or that task busy-spins forever and
        // starves CPU0's idle task.
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
            stop();
            return false;
        }
        s_hostInstalled = true;
        xTaskCreatePinnedToCore(usbLibTask, "usb_lib", 4096, nullptr, 6, &s_libTask, 0);
    }

    usbh_cdc_driver_config_t drvCfg = {};
    drvCfg.task_stack_size = 4096;
    drvCfg.task_priority = 5;
    drvCfg.task_coreid = -1;
    drvCfg.skip_init_usb_host_driver = true;  // we installed usb_host ourselves
    drvCfg.new_dev_cb = newDevCb;
    drvCfg.user_data = nullptr;
    esp_err_t err = usbh_cdc_driver_install(&drvCfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "driver_install: %s", esp_err_to_name(err));
        stop();
        return false;
    }

    // Wait for the device to attach and its descriptors to be parsed (up to
    // ~6s; the C5 needs to boot after VBUS comes up before it enumerates).
    s_preferredValid = false;
    for (int i = 0; i < 300 && !s_preferredValid; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (!s_preferredValid) {
        ESP_LOGW(TAG, "no CDC device on USB-A (is the C5 plugged in / powered?)");
        stop();
        return false;
    }

    usbh_cdc_device_config_t devCfg = {};
    devCfg.vid = CDC_HOST_ANY_VID;  // bind whatever CDC device is on USB-A
    devCfg.pid = CDC_HOST_ANY_PID;
    devCfg.itf_num = s_preferredItf;
    devCfg.rx_buffer_size = RX_BUF;
    devCfg.tx_buffer_size = TX_BUF;
    devCfg.cbs.connect = nullptr;
    devCfg.cbs.disconnect = nullptr;
    devCfg.cbs.recv_data = nullptr;  // we poll the rx ringbuffer instead
    devCfg.cbs.notif_cb = nullptr;
    devCfg.cbs.user_data = nullptr;
    err = usbh_cdc_create(&devCfg, &s_cdc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_create: %s", esp_err_to_name(err));
        s_cdc = nullptr;
        stop();
        return false;
    }

    // A CP2102-bridge C5 is vendor-class: enable the UART + set 115200 8N1
    // and raise DTR/RTS via CP210x vendor requests, or no data flows. A
    // native USB-Serial-JTAG C5 needs none of this (and STALLs
    // SET_LINE_CODING), so we gate on the VID.
    if (s_vid == CP210X_VID) {
        cp210xInit(s_cdc, static_cast<uint16_t>(s_preferredItf));
    }
    started = true;
    return true;
}

void Tab5UsbCdcService::stop() {
    // Symmetric teardown of everything begin() installs. Previously this
    // only deleted the per-device CDC handle: the CDC driver, the USB host
    // library itself, and usbLibTask all stayed installed/running forever
    // after a stop(), so the USB-A PHY/controller claim (s_hostInstalled)
    // could never be released back for another client, and the task kept
    // spinning on CPU0 even with nothing attached. Not currently reachable
    // in the live firmware (this service isn't wired into BitPirateTask.cpp
    // yet -- see this class's header), but fixed now so begin()/stop() are
    // a real symmetric pair for whenever it is.
    if (s_cdc) {
        usbh_cdc_delete(s_cdc);
        s_cdc = nullptr;
    }

    usbh_cdc_driver_uninstall();

    if (s_hostInstalled) {
        s_allFreed = false;
        esp_err_t freeErr = usb_host_device_free_all();

        // usb_host_device_free_all()'s own contract (usb/usb_host.h) is a
        // real three-way result, not a thing to just fire-and-wait-a-bit
        // for: ESP_OK means every device was already free -- no ALL_FREE
        // event will necessarily follow, so waiting for one here would be
        // waiting for something that may never come. ESP_ERR_NOT_FINISHED
        // means an event genuinely is coming, and that's the only case
        // that actually needs the wait below. Anything else is a real
        // error this function can't recover from by waiting.
        bool readyToUninstall = false;
        if (freeErr == ESP_OK) {
            readyToUninstall = true;
        } else if (freeErr == ESP_ERR_NOT_FINISHED) {
            // usb_host_uninstall() hard-requires every device to have been
            // freed first; usbLibTask is the only thing allowed to call
            // usb_host_lib_handle_events() (must not be called from two
            // threads at once), so wait for it to observe and publish the
            // ALL_FREE flag rather than polling the host lib from here too.
            for (int i = 0; i < 100 && !s_allFreed; i++) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            readyToUninstall = s_allFreed;
        } else {
            ESP_LOGW(TAG, "usb_host_device_free_all: %s", esp_err_to_name(freeErr));
        }

        if (readyToUninstall) {
            // Deleting the pump task before uninstall is the standard IDF
            // order (by the time ALL_FREE has fired, its only job -- calling
            // usb_host_lib_handle_events() -- is done; uninstall itself
            // doesn't need it running). But if uninstall then fails anyway
            // for some other reason, s_hostInstalled stays true below to
            // mean "still installed, don't reinstall" -- and that claim
            // must stay backed by a real running pump task, or a later
            // begin() (which only creates one inside `if (!s_hostInstalled)`)
            // would skip over creating it, inheriting an installed-but-
            // unpumped host library that silently never processes an event
            // again.
            if (s_libTask) {
                vTaskDelete(s_libTask);
                s_libTask = nullptr;
            }

            esp_err_t err = usb_host_uninstall();
            if (err != ESP_OK) {
                // Leaves s_hostInstalled true on purpose: the PHY/controller
                // claim was NOT actually released, so a later begin() must
                // keep treating it as still installed rather than attempting
                // (and failing) a second usb_host_install(). Restore the
                // pump task to keep that claim honest -- see comment above.
                ESP_LOGW(TAG, "usb_host_uninstall: %s (PHY claim not released)", esp_err_to_name(err));
                xTaskCreatePinnedToCore(usbLibTask, "usb_lib", 4096, nullptr, 6, &s_libTask, 0);
            } else {
                s_hostInstalled = false;
            }
        } else {
            // Devices never actually freed in time (or free_all() itself
            // errored) -- leave usbLibTask running and the host library
            // installed rather than tearing down a task that a still-
            // attached device may still need. A later stop() can retry.
            ESP_LOGW(TAG, "stop(): device(s) not freed in time, leaving USB host library installed");
        }
        s_allFreed = false;
    }

    // Symmetric with begin()'s explicit enable above -- de-energizing the
    // port on stop() is always safe (equivalent to a normal unplug for
    // whatever's attached) and leaves the board back in its default,
    // VBUS-off state rather than depending on a later begin() to turn it
    // off cleanly first.
    tab5_ioexp_set(USB5V_EN_EXPANDER, USB5V_EN_BIT, 0);

    started = false;
}

bool Tab5UsbCdcService::isReady() const {
    return s_cdc != nullptr;
}

bool Tab5UsbCdcService::available() const {
    if (!s_cdc) return false;
    size_t n = 0;
    return usbh_cdc_get_rx_buffer_size(s_cdc, &n) == ESP_OK && n > 0;
}

char Tab5UsbCdcService::read() {
    if (!s_cdc) return '\0';
    uint8_t c = 0;
    size_t len = 1;
    if (usbh_cdc_read_bytes(s_cdc, &c, &len, 0) == ESP_OK && len == 1) {
        return static_cast<char>(c);
    }
    return '\0';
}

void Tab5UsbCdcService::write(char c) {
    if (s_cdc) usbh_cdc_write_bytes(s_cdc, reinterpret_cast<const uint8_t*>(&c), 1, pdMS_TO_TICKS(200));
}

void Tab5UsbCdcService::write(const char* str) {
    if (s_cdc && str) usbh_cdc_write_bytes(s_cdc, reinterpret_cast<const uint8_t*>(str), strlen(str), pdMS_TO_TICKS(200));
}

void Tab5UsbCdcService::write(const std::string& str) {
    if (s_cdc && !str.empty()) usbh_cdc_write_bytes(s_cdc, reinterpret_cast<const uint8_t*>(str.data()), str.size(), pdMS_TO_TICKS(200));
}

void Tab5UsbCdcService::flush() {
    if (s_cdc) usbh_cdc_flush_rx_buffer(s_cdc);
}
