#pragma once

/*
 * Tab5TouchInput — IInput bridge over Tab5Gui's event-driven LVGL widgets,
 * plus the real M5Stack Tab5 keyboard accessory.
 *
 * Every interactive entry point in lib/BitPirateCore (CommandLineManager,
 * UserInputManager, ...) assumes a blocking, char-by-char input stream --
 * exactly IInput's contract. LVGL widgets are event-driven, not a stream,
 * so this class's job is making them LOOK like one to everything upstream
 * rather than inventing a second input path: Tab5Gui's keyboard/Send/
 * history-strip event callbacks push characters into a shared FreeRTOS
 * queue (tab5gui::charQueue()) as they happen; this class just drains it,
 * falling back to the real keyboard accessory first on every poll (cheap
 * to check even when absent -- orion_kbd_ready() is a flag check, no I2C
 * traffic). CommandLineManager/UserInputManager need zero changes either
 * way -- they just see characters arrive, same as before.
 */

#include <string>
#include <vector>

#include "Interfaces/IInput.h"
#include "Tab5ArrowRepeat.h"

class Tab5TouchInput : public IInput {
public:
    Tab5TouchInput() = default;
    ~Tab5TouchInput() override = default;

    void waitPress(uint32_t timeoutMs = 0) override;

    // Runs orion_kbd_init() (idempotent) and reports whether the real
    // keyboard accessory answered. tab5gui::begin() must already have run
    // (BitPirateTask.cpp calls it first, before constructing this class)
    // -- this class owns only the input bridge, not GUI bring-up.
    bool begin();

protected:
    char handlerImpl() override;
    char readCharImpl() override;

private:
    bool kbdChecked_ = false;
    std::string pendingLine_;             // for deriving the history-strip's recent-command list
    std::vector<std::string> recentCommands_;
    Tab5ArrowRepeat arrows_;

    char pollOnce();
    void trackForHistory(char c);
};
