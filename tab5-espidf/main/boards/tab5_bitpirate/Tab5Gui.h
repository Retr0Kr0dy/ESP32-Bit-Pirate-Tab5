#pragma once

/*
 * Tab5Gui — the real LVGL widget tree for the Bit Pirate GUI: output
 * panel, input bar (live-edit-line label + Send/AI/Files buttons), and a
 * footer band that shows either the history strip or the on-screen
 * keyboard (mutually exclusive) — see Tab5Layout.h's header comment for
 * the full region layout.
 *
 * Also owns the display-flush/touch-read driver bridge into the existing
 * proven SIC/display.c pixel path — unchanged from the LVGL feasibility
 * spike (lvgl_spike.c, now retired): full_refresh mode (this panel does a
 * full-frame DMA flip per present regardless, confirmed earlier), one
 * sgfx_blit()+sgfx_present() per LVGL frame, through the exact same
 * rotation-aware pixel path every draw call already used under SGFX.
 * LVGL's own object/style heap and the one 1280x720 RGB565 framebuffer
 * both live in PSRAM (CONFIG_LV_MEM_CUSTOM + a heap_caps_malloc-backed
 * allocator), not the ~274KB of internal RAM everything else budgets
 * tightly.
 *
 * Pure LVGL plumbing — no Bit Pirate business logic lives here.
 * Tab5TerminalView (ITerminalView) calls appendOutput()/setInputLineText()
 * /clearOutput() to render. Tab5TouchInput (IInput) is the only caller of
 * charQueue()/setHistoryEntries() — see that file's header for the
 * char-stream bridge design: every character-producing input source (the
 * on-screen keyboard's real key events, the Send button, a tapped
 * history-strip entry, and — from Tab5TouchInput itself — the real
 * M5Stack keyboard accessory) funnels into the SAME queue, so
 * CommandLineManager/UserInputManager (upstream lib/BitPirateCore,
 * unmodified) see one ordinary blocking character stream regardless of
 * which input source actually produced it.
 *
 * One process-wide instance (there is only ever one screen) — a
 * free-function namespace API, not a class, matching display.c/
 * touch_bridge.c's own style for the same reason.
 */

#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace tab5gui {

// lv_init(), display/touch driver registration, builds the full widget
// tree, starts the LVGL tick+timer background tasks. Call once, after
// orion_display_ready() (BitPirateTask.cpp does this first, before
// constructing anything else).
void begin();

// ---- Output panel (Tab5TerminalView's rendering target) -----------------
// `markup` may use LVGL recolor syntax ("#RRGGBB text#") for the [FAIL]/
// [ OK ]/[WARN] status lines — Tab5TerminalView is responsible for
// escaping any literal '#' already in the underlying text (doubled, per
// LVGL's own recolor-escape convention) before wrapping it in a span.
// Include a trailing '\n' yourself if you want one (this mirrors
// print()/println()'s own distinction, not added here).
void appendOutput(const std::string& markup);
void clearOutput();

// Registers Tab5DeviceView's lazily-created trace canvas (lv_obj_t*, void*
// to avoid pulling lvgl.h into Tab5DeviceView.h) so appendOutput()/
// clearOutput() can auto-hide it on new text output -- the trace canvas sits
// in the SAME screen region as the output panel and is shown via
// move-to-foreground with no corresponding close button (unlike the file
// browser's modal overlay, which the user explicitly dismisses), so without
// this, any text printed after a graph (e.g. a pin-override confirmation
// prompt) stays hidden behind the still-foregrounded canvas. Call once, from
// Tab5DeviceView::ensureTraceCanvas().
void registerTraceCanvas(void* canvas);

// Flushes the direct_mode cross-buffer catch-up queued by appendOutput()/
// clearOutput(), if any -- call this from the input-wait idle path
// (Tab5TouchInput::pollOnce()), not after every single output call (see
// Tab5Gui.cpp's definition for why that regressed badly).
void drainPendingSyncCatchUp();

// ---- Input bar's live-edit line (Tab5TerminalView's rendering target) ---
// Empty string shows the placeholder ("Enter command"), matching the real
// web page's actual placeholder text.
void setInputLineText(const std::string& text);

// ---- History strip (Tab5TouchInput derives the recall list; this only
// renders it and reports taps back through charQueue()) ------------------
void setHistoryEntries(const std::vector<std::string>& entries);

// ---- Cross-task LVGL lock (same recursive mutex appendOutput()/etc. take
// internally) -- exposed so OTHER callers that need to touch LVGL objects
// directly from outside lvTimerTask (e.g. Tab5DeviceView's graphical trace
// drawing, which calls lv_canvas_* APIs) can hold it too, instead of
// re-deriving their own locking and risking the exact cross-task LVGL
// corruption bug this mutex exists to prevent. Recursive, so nesting with
// an outer caller that already holds it (there currently is none, but
// this makes it safe either way) is fine. ----------------------------------
void lvglLockAcquire();
void lvglLockRelease();

// ---- Character bridge (Tab5TouchInput drains this) -----------------------
// Populated by: the Send button (pushes '\n'), the real on-screen
// keyboard's key events (see Tab5Gui.cpp — backspace/enter/arrows mapped
// to the same codes a real keystroke stream would send), and a tapped
// history-strip entry (pushes its full text, one char at a time, in
// order). Tab5TouchInput also pushes physical-keyboard-accessory
// characters into this SAME queue.
QueueHandle_t charQueue();

} // namespace tab5gui
