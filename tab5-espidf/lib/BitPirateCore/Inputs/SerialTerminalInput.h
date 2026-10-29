#pragma once

#include <Interfaces/IInput.h>
#include <Interfaces/IHostSerial.h>
#include <Arduino.h>
#include <vector>

class SerialTerminalInput : public IInput {
public:
    explicit SerialTerminalInput(IHostSerial& hostSerial)
        : hostSerial(hostSerial) {}

    void waitPress(uint32_t timeoutMs) override;

protected:
    char handlerImpl() override;
    char readCharImpl() override;

private:
    IHostSerial& hostSerial;
};
