#pragma once

#include <Arduino.h>
#include <string>
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_helpers.h"
#include "Interfaces/IUsbS3Service.h"
#include "esp_log.h"
#include <sstream>

// Device-mode HID/MSC: native esp_tinyusb bridge (main/usb_device.h), NOT
// Arduino's USB.h/USBHIDKeyboard.h/USBMSC.h -- that wrapper never compiles
// its real bodies against this project's pinned arduino-esp32/esp_tinyusb
// version combo (CONFIG_TINYUSB_ENABLED, the symbol it gates on, is never
// defined by this esp_tinyusb release). See usb_device.h's header comment
// for the full story. Host mode below is unaffected either way (raw
// usb/usb_host.h calls only).
#include "usb_device.h"

class UsbS3Service : public IUsbS3Service {
public:
    UsbS3Service();

    // Status
    bool isKeyboardActive() const;
    bool isStorageActive() const;
    bool isMouseActive() const;
    bool isGamepadActive() const;
    bool isHostActive() const;
    bool isSystemControlActive() const;

    // Keyboard actions
    void keyboardBegin();
    void keyboardSendString(const std::string& text);
    void keyboardSendChunkedString(const std::string& data, size_t chunkSize, unsigned long delayBetweenChunks);

    // Mass Storage mode
    void storageBegin(uint8_t cs, uint8_t clk, uint8_t miso, uint8_t mosi);

    // Mouse actions
    void mouseBegin();
    void mouseMove(int x, int y);
    void mouseClick(int button);
    void mouseRelease(int button);

    // Gamepad actions
    void gamepadBegin();
    void gamepadPress(const std::string& name);

    // Host 
    bool usbHostBegin();
    std::string usbHostTick();
    void usbHostEnd();
    static void hostClientEventCb(const usb_host_client_event_msg_t *event_msg, void *arg);

    // System control
    void systemControlBegin();
    void systemControlEnd();
    void systemSleep();
    void systemWake();
    void systemPowerOff(uint32_t holdMs = 10);

    // Config
    void configure(const char* productStr, const char* manufacturerStr, const char* serialStr, uint16_t vid, uint16_t pid, const char* webUSBString);
    void reset();
    std::string getUsbSerialFromEfuseMac();

private:
    bool initialized;

    // HID
    bool gamepadActive = false;
    bool keyboardActive = false;
    bool mouseActive = false;
    bool systemControlActive = false;
    unsigned long hidInitTime;

    // Mass Storage -- backed by main/usb_device.c's usb_msc_attach_sdcard()
    // (direct SD-card handoff, see that file's header comment); no SPI/read/
    // write callback plumbing needed here anymore, tinyusb_msc_storage owns
    // the block-level read10/write10 handling internally.
    bool storageActive;

    // Host
    bool stopTinyUsbDevice();
    bool hostInstalled = false;
    usb_host_client_handle_t hostClient = nullptr;
    uint8_t devAddr = 0;
    bool attachPending = false;
    bool detachPending = false;
    bool dumpedThisAttach = false;
    inline static const char* TAG_USBHOST = "UsbHost";
};
