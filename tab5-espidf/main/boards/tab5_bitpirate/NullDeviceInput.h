#pragma once

/* The Tab5 has no secondary hardware-key input. This intentionally does
 * not consume the terminal character stream: CommandLineManager polls the
 * device-input interface separately, and must not steal keystrokes. */

#include "Interfaces/IInput.h"
#include "Data/InputKeys.h"

class NullDeviceInput : public IInput {
public:
    char handler() override { return KEY_NONE; }
    char readChar() override { return KEY_NONE; }
    void waitPress(uint32_t /*timeoutMs*/ = 0) override {}
};
