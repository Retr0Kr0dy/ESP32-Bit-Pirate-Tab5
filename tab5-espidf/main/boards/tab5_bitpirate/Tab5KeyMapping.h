#pragma once

// UI-agnostic adapter: the A164 press/release events share the on-screen
// keyboard's IInput stream; never pass unrecognized or modifier-only keys.
#include "Data/InputKeys.h"
#include "sic/hal.h"

inline char tab5MapKeyEvent(const sic_key_event_t& ev) {
    if (!ev.pressed) return KEY_NONE;
    switch (ev.code) {
        case SIC_KEY_UP:        return KEY_ARROW_UP;
        case SIC_KEY_DOWN:      return KEY_ARROW_DOWN;
        case SIC_KEY_LEFT:      return KEY_ARROW_LEFT;
        case SIC_KEY_RIGHT:     return KEY_ARROW_RIGHT;
        case SIC_KEY_ENTER:     return KEY_OK;
        case SIC_KEY_BACKSPACE:
        case SIC_KEY_DEL:       return KEY_DEL;
        case SIC_KEY_TAB:       return KEY_TAB_CUSTOM;
        case SIC_KEY_ESC:       return KEY_ESC_CUSTOM;
        case SIC_KEY_CTRL:
        case SIC_KEY_ALT:
        case SIC_KEY_SHIFT:
        case SIC_KEY_FN:
        case SIC_KEY_OPT:
        case SIC_KEY_CAPS:      return KEY_NONE;
        default:               return ev.ascii ? ev.ascii : KEY_NONE;
    }
}
