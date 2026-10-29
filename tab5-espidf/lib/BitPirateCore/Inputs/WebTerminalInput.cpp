#include "WebTerminalInput.h"

WebTerminalInput::WebTerminalInput(WebSocketServer& server) : server(server) {}

char WebTerminalInput::handlerImpl() {
    return server.readCharBlocking();
}

char WebTerminalInput::readCharImpl() {
    return server.readCharNonBlocking();
}

void WebTerminalInput::waitPress(uint32_t timeoutMs) {
    (void)timeoutMs; // currently not used
    server.readCharBlocking();
}
