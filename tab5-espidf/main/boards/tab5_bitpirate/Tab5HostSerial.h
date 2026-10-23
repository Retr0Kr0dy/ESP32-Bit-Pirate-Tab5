#pragma once

/*
 * Tab5HostSerial — IHostSerial backed by main/usb_device.c's native
 * esp_tinyusb CDC-ACM, for the one-shot USB "adapter persona" boot path
 * (BootModeConfigurator::configure()). The original's DefaultHostSerial
 * wraps Arduino's native-USB Serial, which doesn't compile real bodies on
 * this arduino-esp32/esp_tinyusb combo (see usb_device.h) -- this calls
 * usb_device.c directly instead.
 *
 * RX is push-based (usb_device.c calls back from TinyUSB's task whenever
 * data arrives); IHostSerial's available()/read()/peek() are pull-based --
 * a small FreeRTOS byte queue bridges the two.
 */

#include "Interfaces/IHostSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class Tab5HostSerial : public IHostSerial {
public:
    Tab5HostSerial();
    ~Tab5HostSerial() override;

    void begin(unsigned long baud) override;
    void waitReady() override;

    int available() override;
    int availableForWrite() override;
    int peek() override;
    int read() override;
    size_t readBytes(uint8_t* buffer, size_t length) override;

    size_t write(uint8_t value) override;
    size_t write(const uint8_t* buffer, size_t length) override;
    size_t print(const char* text) override;
    size_t println(const char* text) override;
    size_t println() override;

    void flush() override;
    bool setRxBufferSize(size_t size) override;
    void setTimeout(unsigned long timeoutMs) override;
    uint32_t baudRate() override;
    void disableReboot() override;
    void waitForPress() override;

private:
    QueueHandle_t rxQueue_ = nullptr;
    unsigned long timeoutMs_ = 1000;
    int peeked_ = -1; // -1 = none buffered; a real byte is never negative
};
