#pragma once

/*
 * Tab5Layout.h — shared screen-region constants and the web-UI-matching
 * color palette, used by Tab5Gui.cpp (the only place that builds LVGL
 * widgets now).
 *
 * ---------------------------------------------------------------------
 * LVGL migration: this header used to also carry a hand-rolled Button/
 * hitTest() type and per-widget X/Y constants for InputBar/HistoryStrip/
 * TouchKeyboard, all computed by hand against raw SGFX draw calls. LVGL
 * owns hit-testing and most layout math natively now (flex containers,
 * lv_obj_set_pos/size) — those constants and the Button/hitTest() helper
 * are gone; only the handful of top-level band Y positions LVGL itself
 * needs (computed once, by hand, below) remain.
 *
 * Region layout (landscape 1280x720 — see orientation note below):
 *
 *   +----------------------------------------------------------+ y=0
 *   |  Output panel (read-only lv_textarea, real LVGL          |
 *   |  scrollback) -- height depends on footer state below.    |
 *   +----------------------------------------------------------+
 *   |  Input bar (fixed height, two possible Y positions):     |
 *   |  text-zone label (live-edit line / placeholder) + Send/  |
 *   |  AI/Files buttons.                                        |
 *   +----------------------------------------------------------+
 *   |  Footer band: history strip OR on-screen keyboard         |
 *   |  (mutually exclusive, same band, two different heights — |
 *   |  the keyboard needs real room for 4 rows of comfortably-  |
 *   |  sized keys; the history strip doesn't).                  |
 *   +----------------------------------------------------------+ y=720
 *
 * When the keyboard is summoned, the input bar moves up to sit directly
 * above it (and the output panel's height shrinks to match) — matching
 * the real iOS/Android convention the user asked for (the keyboard
 * overlays/occupies screen space, the input stays visible just above it)
 * — NOT a reflow of the output panel's actual scrollback content, just a
 * resize of how much of it is currently visible; LVGL's own scrolling
 * handles this correctly with no manual redraw-from-buffer logic needed
 * (unlike the old hand-rolled SGFX version this replaces).
 *
 * Orientation: LANDSCAPE ONLY — main.c picks rotation 1 or 3 at boot (IMU
 * reading, landscape-only), never portrait. Portrait remains explicitly
 * DEFERRED, not implemented.
 */

#include <cstdint>

namespace tab5 {

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;
constexpr int kPad     = 10;

// Touch-target sizing for this specific panel (294 PPI, bench-tool
// viewing distance): 80px absolute floor, ~120px for primary/frequent
// buttons (Send, history-strip entries). See the port plan's UI-research
// findings for the derivation.
constexpr int kTouchPrimary = 120;

constexpr int kInputBarH       = kTouchPrimary;
constexpr int kFooterHistoryH  = 140; // footer band height when showing the history strip
constexpr int kFooterKeyboardH = 420; // footer band height when showing the on-screen keyboard

// ---- Two complete band layouts, history-footer vs. keyboard-footer ------
constexpr int kFooterYHistory  = kScreenH - kPad - kFooterHistoryH;
constexpr int kInputYHistory   = kFooterYHistory - kPad - kInputBarH;
constexpr int kOutputHHistory  = kInputYHistory - kPad - kPad;

constexpr int kFooterYKeyboard = kScreenH - kPad - kFooterKeyboardH;
constexpr int kInputYKeyboard  = kFooterYKeyboard - kPad - kInputBarH;
constexpr int kOutputHKeyboard = kInputYKeyboard - kPad - kPad;

// ---- Colors (0xRRGGBB, for lv_color_hex()) ------------------------------
// Matches ESP32-Bit-Pirate's own web UI (webui/style.h): near-black
// background, pure-green terminal text, cyan-green accent, warm amber for
// the AI/Files buttons (AI is an inert placeholder; Files opens a working
// browser -- see filesBtnClickedCb()/aiBtnClickedCb() in Tab5Gui.cpp),
// red/amber for status markers.
constexpr uint32_t kColorBg          = 0x1a1a1a;
constexpr uint32_t kColorBgDeep      = 0x0f0f0f; // gradient's dark end
constexpr uint32_t kColorInputBg     = 0x1c1c1c;
constexpr uint32_t kColorGreen       = 0x00ff00;
constexpr uint32_t kColorAccent      = 0x00ffcc;
constexpr uint32_t kColorBorder      = 0x333333;
constexpr uint32_t kColorChrome      = 0x444444;
constexpr uint32_t kColorWarn        = 0xffdc7d;
constexpr uint32_t kColorFail        = 0xff5555;
constexpr uint32_t kColorPlaceholder = 0x666666;

} // namespace tab5
