// TAB5 PORT: "screenshot" hook, same extern-hook pattern as "webui"/"ai" in
// ActionDispatcher.cpp. Dumps the current screen as a base64 BMP over the
// same USB-Serial-JTAG console everything else logs to -- see display.h's
// orion_display_dump_screenshot_b64() for the actual capture/encode.

#include "Interfaces/ITerminalView.h"

extern "C" void orion_display_dump_screenshot_b64(void);

void bitpirate_screenshot_hook(ITerminalView& terminalView) {
    terminalView.println("Capturing screenshot to serial console...");
    orion_display_dump_screenshot_b64();
    terminalView.println("Screenshot sent.");
}
