#pragma once

#include <vector>
#include <string>

class PinoutConfig {
public:
    // Set the current mode (e.g. "I2C", "SPI", "UART"...)
    void setMode(const std::string& newMode) {
        mode = newMode;
    }

    // Get the current mode
    const std::string& getMode() const {
        return mode;
    }

    // Set all mappings at once
    void setMappings(const std::vector<std::string>& newMappings) {
        mappings = newMappings;
    }

    // Add a single mapping like "1 RX"
    void addMapping(const std::string& mapping) {
        mappings.push_back(mapping);
    }

    // Get all mappings
    const std::vector<std::string>& getMappings() const {
        return mappings;
    }

    // Get a specific mapping by index
    std::string getMappingAt(size_t index) const {
        if (index < mappings.size()) {
            return mappings[index];
        }
        return "";
    }

    // Clear all mappings
    void clearMappings() {
        mappings.clear();
    }

private:
    std::string mode = "None";
    std::vector<std::string> mappings;
};
