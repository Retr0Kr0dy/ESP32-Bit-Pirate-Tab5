#pragma once

#include <string>
#include <vector>

class Instruction {
public:
    std::string raw;               // Texte brut "[0xA5 r]"
    char prefix;                  // [, >, { ...
    std::vector<std::string> tokens; // Liste tokens : "0xA5", "r", ...

    Instruction(const std::string& rawInput)
        : raw(rawInput), prefix(rawInput.empty() ? '\0' : rawInput[0]) {}

    void addToken(const std::string& token) {
        tokens.push_back(token);
    }
};
