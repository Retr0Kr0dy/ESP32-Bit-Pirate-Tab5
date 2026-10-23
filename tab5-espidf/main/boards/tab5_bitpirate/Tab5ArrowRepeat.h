#pragma once
#include <cstdint>
#include "Tab5KeyMapping.h"

// Small bounded repeat state machine based on upstream Tab5's A164
// implementation. Real events (particularly releases) take precedence over
// repeats. An absent release cannot repeat forever after a disconnect.
class Tab5ArrowRepeat {
public:
    void onEvent(const sic_key_event_t& ev, uint32_t nowMs) {
        if (!ev.pressed) {
            if (ev.code == heldCode_) stop();
            return;
        }
        if (ev.code == SIC_KEY_UP || ev.code == SIC_KEY_DOWN) {
            heldCode_ = ev.code;
            heldSinceMs_ = nowMs;
            nextMs_ = nowMs + 350;
        } else {
            stop();
        }
    }

    char poll(uint32_t nowMs) {
        if (heldCode_ == SIC_KEY_NONE) return KEY_NONE;
        uint32_t elapsed = nowMs - heldSinceMs_;
        if (elapsed >= 30000) { stop(); return KEY_NONE; }
        if ((int32_t)(nowMs - nextMs_) < 0) return KEY_NONE;
        // No backlog bursts after a GUI or command stall. After two seconds
        // the repeats accelerate, exactly as the official Tab5 input intends.
        nextMs_ = nowMs + (elapsed >= 2000 ? 42 : 125);
        return heldCode_ == SIC_KEY_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN;
    }

    void stop() { heldCode_ = SIC_KEY_NONE; }
private:
    sic_keycode_t heldCode_ = SIC_KEY_NONE;
    uint32_t heldSinceMs_ = 0;
    uint32_t nextMs_ = 0;
};
