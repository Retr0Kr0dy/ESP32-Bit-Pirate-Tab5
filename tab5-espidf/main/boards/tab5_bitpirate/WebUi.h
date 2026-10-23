#pragma once

/*
 * WebUi — serves the REAL, unmodified ESP32-Bit-Pirate web UI
 * (webui/{index,style,scripts}.h, vendored verbatim) over a Wi-Fi AP this
 * board creates itself, bridging the page's WebSocket into the same
 * command path the touchscreen GUI drives.
 *
 * Wi-Fi is NOT brought up at boot -- start() must be called explicitly
 * (the "webui" command, ActionDispatcher.cpp's extern hook into this file).
 */

#include "Interfaces/ITerminalView.h"

namespace webui {

// Idempotent -- safe to call again once already running (just reports
// status). Brings up the AP, starts the HTTP+WebSocket server, and installs
// Tab5TerminalView's web-mirror hook so dispatcher output reaches the page.
// Logs progress/result through `log` (the same ITerminalView the touch GUI
// already prints through, so status is visible on-screen too).
void start(ITerminalView& log);

bool isRunning();

} // namespace webui
