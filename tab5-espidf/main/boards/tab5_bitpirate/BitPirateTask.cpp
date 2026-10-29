/* Tab5 adapter for the upstream Bit Pirate ActionDispatcher. */
#include <Arduino.h>
#include <cstdint>

#include "esp_log.h"

#include "Tab5TerminalView.h"
#include "Tab5DeviceView.h"
#include "Tab5TouchInput.h"
#include "Tab5HostSerial.h"
#include "NullDeviceInput.h"
#include "Providers/DependencyProvider.h"
#include "Dispatchers/ActionDispatcher.h"
#include "Services/LittleFsService.h"
#include "Services/NvsService.h"
#include "Configurators/BootModeConfigurator.h"
#include "Enums/TerminalTypeEnum.h"
#include "Tab5Gui.h"
#include "usb_device.h"
#include "Tab5UsbCdcService.h"
#include "BitPirateTask.h"

static const char* TAG = "bitpirate";

extern "C" void bitpirate_task_entry(void* arg) {
    (void)arg;

    initArduino();
    ESP_LOGI(TAG, "initArduino() done");

    // Build the LVGL UI before constructing board views and inputs.
    tab5gui::begin();
    ESP_LOGI(TAG, "tab5gui::begin() done");

    static Tab5DeviceView devView;
    static Tab5TerminalView termView;
    static Tab5TouchInput touchInput;
    static NullDeviceInput nullDeviceInput;

    // Consume upstream one-shot USB adapter modes before the normal shell.
    // No global esp_task_wdt_deinit() workaround here anymore: every
    // one-shot adapter's loop (lib/BitPirateCore/Adapters/*.cpp) now uses a
    // real vTaskDelay() instead of a bare yield() at its idle/wait point --
    // a bare yield() can never let the (lowest-priority) idle task run, so
    // it never feeds that task's watchdog subscription, which is what
    // actually starved it (confirmed on real hardware,
    // B/HARDWARE_TEST_TRACKER.md, T10) -- disabling the watchdog globally
    // was masking that, not fixing it.
    {
        static Tab5HostSerial hostSerial;
        static NvsService nvsService;
        BootModeConfigurator(devView, nullDeviceInput, nvsService, hostSerial).configure();
    }

    devView.initialize();
    termView.initialize();

    // Both the keyboard accessory and the on-screen keyboard feed IInput.
    const bool kbdAttached = touchInput.begin();
    termView.println(kbdAttached
        ? "Keyboard accessory: detected."
        : "Keyboard accessory: not detected -- tap the input bar to use the on-screen keyboard.");

    // USB CDC-ACM mirrors the terminal, as in the upstream serial CLI.
    // HID and MSC share this composite device and remain idle until used.
    usb_device_start();
    usb_cdc_set_rx_callback([](const uint8_t* data, size_t len) {
        QueueHandle_t q = tab5gui::charQueue();
        if (!q) return;
        for (size_t i = 0; i < len; ++i) {
            char c = (char)data[i];
            xQueueSend(q, &c, 0);
        }
    });
    Tab5TerminalView::setCdcMirror([](const char* data, size_t len, bool withNewline) {
        usb_cdc_write(data, len);
        if (withNewline) usb_cdc_write("\r\n", 2);
    });
    termView.println("USB-serial CLI: ready (plug in a USB cable and open it as a serial port).");

    // Keep business logic upstream: only the board-facing view and input
    // implementations differ from the original firmware.
    static LittleFsService littleFsService;
    // Tab5UsbCdcService stays unwired: usb_host_install() hangs CPU0 on real
    // hardware (P4's 2nd USB-OTG controller fights the native device-mode
    // PHY). Falls back to the generic uartService until that's fixed.
    static DependencyProvider provider(termView, devView, touchInput, nullDeviceInput, littleFsService);
    static ActionDispatcher realDispatcher(provider);
    realDispatcher.setup(TerminalTypeEnum::Standalone, "Tab5");
    ESP_LOGI(TAG, "ActionDispatcher (real dispatcher) ready");

    realDispatcher.run(); // never returns
}
