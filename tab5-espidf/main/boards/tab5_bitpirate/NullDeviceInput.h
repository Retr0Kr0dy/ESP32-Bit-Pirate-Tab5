#pragma once

/* The Tab5 has no secondary hardware-key input. This intentionally does
 * not consume the terminal character stream: CommandLineManager polls the
 * device-input interface separately, and must not steal keystrokes. */

#include "Interfaces/IInput.h"
#include "Data/InputKeys.h"

class NullDeviceInput : public IInput {
public:
    void waitPress(uint32_t /*timeoutMs*/ = 0) override {}

protected:
    char handlerImpl() override { return KEY_NONE; }
    char readCharImpl() override { return KEY_NONE; }
};
