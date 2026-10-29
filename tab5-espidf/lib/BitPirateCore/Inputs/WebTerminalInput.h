#pragma once
#include "Interfaces/IInput.h"
#include "Servers/WebSocketServer.h"

class WebTerminalInput : public IInput {
public:
    WebTerminalInput(WebSocketServer& server);

    void waitPress(uint32_t timeoutMs) override;

protected:
    char handlerImpl() override;
    char readCharImpl() override;

private:
    WebSocketServer& server;
};
