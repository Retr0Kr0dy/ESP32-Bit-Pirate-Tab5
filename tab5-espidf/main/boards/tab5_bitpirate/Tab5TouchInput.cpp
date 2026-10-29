#include "Tab5TouchInput.h"

#include <cctype>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "Data/InputKeys.h"
#include "kbd_accessory.h"
#include "Tab5Gui.h"
#include "Tab5KeyMapping.h"

namespace {

// The real web UI's own history strip (webui/scripts.h's addToHistory() +
// .history-area's `overflow-x: auto` CSS) has NO count cap at all -- it
// grows unbounded, newest-on-left, and relies purely on horizontal scroll
// to reach older entries. This used to cap hard at 4 (from when the Tab5
// history strip's own horizontal scroll wasn't actually wired up -- see
// Tab5Gui.cpp's setHistoryEntries()/s_historyCont for that fix), making
// anything past 4 simply unreachable. Now that scrolling works, match
// upstream's effectively-unbounded behavior; a generous finite cap (not
// truly infinite like the browser) just bounds this embedded target's
// memory growth over a very long uptime -- at a few dozen bytes per short
// command string, even this is negligible.
constexpr int kMaxHistoryEntries = 200;

} // namespace

bool Tab5TouchInput::begin() {
    kbdChecked_ = true;
    orion_kbd_init(); // no-op-safe if no keyboard accessory is attached
    return orion_kbd_ready();
}

void Tab5TouchInput::trackForHistory(char c) {
    const bool isEnter = (c == '\n' || c == '\r' || c == KEY_OK);
    const bool isErase = (c == KEY_DEL || c == 127);

    if (isEnter) {
        if (!pendingLine_.empty() &&
            (recentCommands_.empty() || recentCommands_.front() != pendingLine_)) {
            recentCommands_.insert(recentCommands_.begin(), pendingLine_);
            if ((int)recentCommands_.size() > kMaxHistoryEntries) {
                recentCommands_.resize(kMaxHistoryEntries);
            }
            tab5gui::setHistoryEntries(recentCommands_);
        }
        pendingLine_.clear();
        return;
    }

    if (isErase) {
        if (!pendingLine_.empty()) pendingLine_.pop_back();
        return;
    }

    if (std::isprint(static_cast<unsigned char>(c))) pendingLine_.push_back(c);
}

char Tab5TouchInput::pollOnce() {
    if (!kbdChecked_) begin();

    // 1. Real keyboard accessory first, if attached.
    if (orion_kbd_ready()) {
        sic_key_event_t kev;
        if (orion_kbd_poll(&kev) == 1) {
            arrows_.onEvent(kev, static_cast<uint32_t>(esp_timer_get_time() / 1000));
            char mapped = tab5MapKeyEvent(kev);
            if (mapped != KEY_NONE) trackForHistory(mapped);
            return mapped;
        }
        // Repeat only once the real event FIFO is empty. A release must
        // stop scrolling immediately even when the LVGL task is busy.
        char repeat = arrows_.poll(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        if (repeat != KEY_NONE) {
            trackForHistory(repeat);
            return repeat;
        }
    } else {
        arrows_.stop();
    }

    // 2. Whatever Tab5Gui's LVGL widgets (keyboard, Send, history-strip
    // taps) have queued since the last poll.
    char c;
    if (xQueueReceive(tab5gui::charQueue(), &c, 0) == pdTRUE) {
        trackForHistory(c);
        return c;
    }

    // CommandLineManager::readCommand() (unmodified upstream) calls
    // readChar() directly in a bare `while(true) { ...; continue; }` loop
    // with no yield of its own -- the old SGFX-era implementation got away
    // with this because its touch poll was an I2C transaction with real
    // latency, which incidentally throttled it enough to avoid starving
    // the scheduler. Both sources checked above are now near-zero-cost (a
    // flag check, a zero-timeout queue peek), so without an explicit yield
    // here that upstream loop spins at full speed and trips the task
    // watchdog (confirmed on real hardware: ~12s after boot). One tick is
    // the cheapest fix that doesn't change readChar()'s non-blocking
    // contract -- it still returns on this same call, every time.
    // Nothing pending this poll -- the natural point to flush any
    // direct_mode catch-up queued by a just-finished burst of output
    // (see Tab5Gui.cpp's drainPendingSyncCatchUp()). No-ops instantly if
    // nothing's pending, so this costs nothing on the other ~999/1000
    // idle polls where there's genuinely nothing to flush.
    tab5gui::drainPendingSyncCatchUp();

    vTaskDelay(pdMS_TO_TICKS(1));
    return KEY_NONE;
}

char Tab5TouchInput::handlerImpl() {
    char c;
    do {
        c = pollOnce();
        if (c != KEY_NONE) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    return c;
}

char Tab5TouchInput::readCharImpl() {
    return pollOnce();
}

void Tab5TouchInput::waitPress(uint32_t timeoutMs) {
    uint32_t waited = 0;
    while (true) {
        if (pollOnce() != KEY_NONE) return;
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
        if (timeoutMs != 0 && waited >= timeoutMs) return;
    }
}
