/*
 * gui_rotation_bridge.h — thin C-callable notification from display.c's
 * (plain C) auto-rotation poll into Tab5Gui.cpp's (C++) LVGL layer.
 *
 * Why this exists: orion_display_set_rotation() only updates the
 * underlying pixel coordinate mapping (apply_rotation_to_caps() -- s_w/
 * s_h swap, SGFX clip reset). It has no idea LVGL exists, and under
 * direct_mode LVGL only redraws regions something has explicitly
 * invalidated -- a rotation flip changes what every already-composited
 * pixel MEANS without marking anything dirty, so nothing forces a
 * corrective redraw until some unrelated future event (a cursor blink, a
 * touch, new output text) happens to trigger one. That's the rotation
 * "not smooth" / "only seems to work once" symptom: the screen doesn't
 * redraw cleanly on its own, it waits on incidental activity to patch
 * itself up piecemeal.
 *
 * Same C/C++ boundary problem touch_bridge.h solves in the other
 * direction (C exposed to C++) -- Tab5Gui.h can't be included from a .c
 * file at all (unconditional <string>/<vector> includes, a C++
 * namespace, no extern "C" guards), so this is a minimal, separate,
 * genuinely C-compatible header whose one function is implemented in
 * Tab5Gui.cpp.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Forces an immediate full-screen LVGL invalidate + redraw, through the
 * existing cross-task lock. Safe to call before tab5gui::begin() has run
 * (no-ops if the widget tree doesn't exist yet) and safe to call from any
 * task -- that's the whole point, display.c's rotation poll runs on
 * main.c's own task, not lvTimerTask. */
void tab5gui_notify_rotation_changed(void);

#ifdef __cplusplus
}
#endif
