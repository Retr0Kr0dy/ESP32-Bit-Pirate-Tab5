#include "Tab5HostSerial.h"

#include <cstring>

#include "usb_device.h"

namespace {
// Matches CONFIG_TINYUSB_CDC_RX_BUFSIZE (sdkconfig.defaults) -- no reason
// for this software queue to be deeper than TinyUSB's own FIFO behind it.
constexpr int kRxQueueLen = 512;

QueueHandle_t s_activeRxQueue = nullptr; // single instance in practice (one adapter runs at a time)

void rxCallback(const uint8_t* data, size_t len) {
    if (!s_activeRxQueue) return;
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        xQueueSend(s_activeRxQueue, &c, 0);
    }
}
} // namespace

Tab5HostSerial::Tab5HostSerial() {
    rxQueue_ = xQueueCreate(kRxQueueLen, sizeof(char));
}

Tab5HostSerial::~Tab5HostSerial() {
    if (s_activeRxQueue == rxQueue_) s_activeRxQueue = nullptr;
    if (rxQueue_) vQueueDelete(rxQueue_);
}

void Tab5HostSerial::begin(unsigned long /*baud*/) {
    // Baud is meaningless over USB-CDC; accepted only because IHostSerial requires it.
    s_activeRxQueue = rxQueue_;
    usb_cdc_set_rx_callback(rxCallback);
    usb_device_start();
}

void Tab5HostSerial::waitReady() {
    while (!usb_cdc_connected()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int Tab5HostSerial::available() {
    int n = (int)uxQueueMessagesWaiting(rxQueue_);
    return peeked_ >= 0 ? n + 1 : n;
}

int Tab5HostSerial::availableForWrite() {
    return 64; // nominal -- usb_cdc_write() doesn't expose TinyUSB's real FIFO headroom, and no caller here depends on an exact value
}

int Tab5HostSerial::peek() {
    if (peeked_ >= 0) return peeked_;
    char c;
    if (xQueueReceive(rxQueue_, &c, 0) != pdTRUE) return -1;
    peeked_ = (uint8_t)c;
    return peeked_;
}

int Tab5HostSerial::read() {
    if (peeked_ >= 0) {
        int v = peeked_;
        peeked_ = -1;
        return v;
    }
    char c;
    if (xQueueReceive(rxQueue_, &c, 0) != pdTRUE) return -1;
    return (uint8_t)c;
}

size_t Tab5HostSerial::readBytes(uint8_t* buffer, size_t length) {
    size_t got = 0;
    while (got < length) {
        int v;
        TickType_t waited = 0;
        const TickType_t step = pdMS_TO_TICKS(2);
        for (;;) {
            v = read();
            if (v >= 0) break;
            if (waited >= pdMS_TO_TICKS(timeoutMs_)) return got; // real timeout -- matches Stream::readBytes() giving up early
            vTaskDelay(step);
            waited += step;
        }
        buffer[got++] = (uint8_t)v;
    }
    return got;
}

size_t Tab5HostSerial::write(uint8_t value) {
    usb_cdc_write((const char*)&value, 1);
    return 1;
}

size_t Tab5HostSerial::write(const uint8_t* buffer, size_t length) {
    usb_cdc_write((const char*)buffer, length);
    return length;
}

size_t Tab5HostSerial::print(const char* text) {
    size_t len = strlen(text);
    usb_cdc_write(text, len);
    return len;
}

size_t Tab5HostSerial::println(const char* text) {
    size_t len = print(text);
    usb_cdc_write("\r\n", 2);
    return len + 2;
}

size_t Tab5HostSerial::println() {
    usb_cdc_write("\r\n", 2);
    return 2;
}

void Tab5HostSerial::flush() {
    // No-op: usb_cdc_write() already flushes immediately on every call.
}

bool Tab5HostSerial::setRxBufferSize(size_t /*size*/) {
    return true; // fixed-size queue already sized for this -- see kRxQueueLen
}

void Tab5HostSerial::setTimeout(unsigned long timeoutMs) {
    timeoutMs_ = timeoutMs;
}

uint32_t Tab5HostSerial::baudRate() {
    return 115200; // nominal -- USB-CDC has no real baud; adapters that report this to the host print a number, not drive hardware with it
}

void Tab5HostSerial::disableReboot() {
    // No-op: Arduino's "1200bps touch reboots into bootloader" feature was never wired up here.
}

void Tab5HostSerial::waitForPress() {
    while (available() == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
