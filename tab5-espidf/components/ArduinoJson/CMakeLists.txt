# Vendored from PlatformIO registry (bblanchon/ArduinoJson @ 7.4.3, satisfies
# ESP32-Bit-Pirate/platformio.ini's "bblanchon/ArduinoJson@^7.3.0" pin).
# Header-only; INTERFACE-style registration (no SRCS). Needed by
# lib/BitPirateCore/Transformers/JsonTransformer.h's unconditional
# <ArduinoJson.h>.
idf_component_register(
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
