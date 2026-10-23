#pragma once

#include "Interfaces/IUartService.h"
#include <string>
#include <vector>

// Expander transport over Tab5's USB-A host port (CDC-ACM, e.g. C5/CP210x),
// via the vendored iot_usbh_cdc driver (components/iot_usbh_cdc). Ported
// from the official p4-tab5 branch, dropped to plain ESP-IDF calls. Not
// currently wired in -- see BitPirateTask.cpp. Only
// begin()/available()/read()/write()/flush() are functional; the rest are
// no-ops (no GPIO pins on a USB pipe).
class Tab5UsbCdcService : public IUartService {
public:
    ~Tab5UsbCdcService() override = default;

    bool begin();   // install USB host + CDC driver, wait for and open the device
    void stop();
    bool isReady() const;

    // --- functional (used by the Expander bridge) ---
    bool available() const override;
    char read() override;
    void write(char c) override;
    void write(const char* str) override;
    void write(const std::string& str) override;
    void flush() override;

    // configure() has no pins on a USB pipe -> it just brings up the host link.
    void configure(unsigned long, uint32_t, uint8_t, uint8_t, bool,
                   HardwareSerial* = nullptr, bool = false) override { begin(); }

    // --- not applicable to a USB-CDC pipe: no-ops / empty ---
    void release() override { stop(); }
    void print(const std::string& msg) override { write(msg); }
    void println(const std::string& msg) override { write(msg); write("\r\n"); }
    std::string readLine() override { return {}; }
    void setRxFIFOFull(uint8_t) override {}
    void setDefaultRxFIFOFull() override {}
    std::string executeByteCode(const std::vector<ByteCode>&) override { return {}; }
    void switchBaudrate(unsigned long) override {}
    void clearUartBuffer() override { flush(); }
    void end() override { stop(); }
    bool isInstalled() const override { return isReady(); }
    uint32_t buildUartConfig(uint8_t, char, uint8_t) override { return 0; }

    void initXmodem() override {}
    bool xmodemReceiveToFile(fs::File&) override { return false; }
    bool xmodemSendFile(fs::File&) override { return false; }
    void setXmodemReceiveHandler(bool (*)(void*, size_t, uint8_t*, size_t)) override {}
    void setXmodemSendHandler(void (*)(void*, size_t, uint8_t*, size_t)) override {}
    void setXmodemBlockSize(int32_t) override {}
    void setXmodemIdSize(int8_t) override {}
    void setXmodemCrc(bool) override {}
    int32_t getXmodemBlockSize() const override { return 0; }
    int8_t getXmodemIdSize() const override { return 0; }

    UartPinActivity measureUartActivity(uint8_t, uint32_t = 100, bool = true) override { return {}; }
    std::vector<UartPinActivity> scanUartActivity(const std::vector<uint8_t>&, uint32_t = 100,
                                                  uint32_t = 10, bool = true) override { return {}; }
    uint32_t detectBaudByEdge(uint8_t, uint32_t = 5000, uint32_t = 300, uint32_t = 30,
                              bool = true) override { return 0; }
    std::vector<uint32_t> getBaudList() const override { return {}; }

private:
    bool started = false;
};
