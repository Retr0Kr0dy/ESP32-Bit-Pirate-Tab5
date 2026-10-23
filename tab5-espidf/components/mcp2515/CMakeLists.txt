# Vendored from github.com/autowp/arduino-mcp2515 @ v1.2.1 (matching
# ESP32-Bit-Pirate/platformio.ini's "autowp/autowp-mcp2515@^1.2.1" pin).
# Needed by lib/BitPirateCore/Services/CanService.h's unconditional
# <mcp2515.h>.
idf_component_register(
    SRCS "src/mcp2515.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
