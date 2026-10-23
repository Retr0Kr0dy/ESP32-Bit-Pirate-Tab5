#include "WebRequestTransformer.h"

TerminalCommand WebRequestTransformer::toTerminalCommand(const std::string& body) {
    JsonDocument doc;

    DeserializationError err = deserializeJson(doc, body);
    if (!err && doc.containsKey("command")) {
        std::string commandStr = doc["command"].as<std::string>();
        return TerminalCommand(commandStr);
    }

    return TerminalCommand(body);
}

std::string WebRequestTransformer::toTerminalCommandRaw(const std::string& body) {
    JsonDocument doc;

    DeserializationError err = deserializeJson(doc, body);
    if (!err && doc.containsKey("command")) {
        std::string commandStr = doc["command"].as<std::string>();
        return commandStr;
    }

    return body;
}

std::string WebRequestTransformer::toJsonResponse(const std::string& cliOutput) {
    JsonDocument doc;

    doc["result"] = cliOutput;

    std::string output;
    serializeJson(doc, output);
    return output;
}
