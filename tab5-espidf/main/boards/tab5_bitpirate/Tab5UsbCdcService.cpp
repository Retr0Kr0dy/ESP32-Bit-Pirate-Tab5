#include "Tab5UsbCdcService.h"

#include <cstring>
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "iot_usbh_cdc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"

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

    // Unlike the official branch, no VBUS-enable step here: Tab5's
    // EXT1_IOEXP's USB5V_EN bit is already driven high at boot by
    // tab5_ioexp_init() (see lib/SIC/src/boards/tab5/ioexpander.c),
    // regardless of whether this service ever runs.

    if (!s_hostInstalled) {
        usb_host_config_t hostCfg = {};
        hostCfg.skip_phy_setup = false;
        hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
        esp_err_t err = usb_host_install(&hostCfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
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
        return false;
    }

    // Wait for the device to attach and its descriptors to be parsed (up to
    // ~6s; the C5 needs to boot after VBUS comes up before it enumerates).
    s_preferredValid = false;
    for (int i = 0; i < 300 && !s_preferredValid; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (!s_preferredValid) {
        ESP_LOGW(TAG, "no CDC device on USB-A (is the C5 plugged in / powered?)");
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
    if (s_cdc) {
        usbh_cdc_delete(s_cdc);
        s_cdc = nullptr;
    }
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
