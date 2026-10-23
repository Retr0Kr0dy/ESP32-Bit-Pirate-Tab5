# Vendored from ESP32-Bit-Pirate/lib/SX126x-Arduino (upstream's own
# vendored copy of beegee-tokyo/SX126x-Arduino v2.0.32, not a registry
# fetch -- matches exactly what this project's other "upstream already
# vendors this in lib/" cases do, e.g. SmartRC-CC1101-Driver-Lib). Whole
# src/ tree copied verbatim (mac/radio/sx126x/boards -- all
# platform-generic Arduino C++, confirmed no non-Arduino-only subtree like
# RF24's excluded utility/ dir). Needed by
# lib/BitPirateCore/Services/LoRaService.h's unconditional
# <SX126x-Arduino.h> (LoRa + Meshtastic modes).
file(GLOB_RECURSE SX126X_SRCS "src/*.cpp")
# Exclude the other MCU families' board-support files (nRF52/RP2040/RAK11300
# SDK-specific, would not compile against ESP-IDF/arduino-esp32 at all) --
# keep only the generic mac/radio/system logic plus the espressif board
# support this project actually needs.
list(FILTER SX126X_SRCS EXCLUDE REGEX "/boards/mcu/nrf52832/")
list(FILTER SX126X_SRCS EXCLUDE REGEX "/boards/mcu/rp2040/")
list(FILTER SX126X_SRCS EXCLUDE REGEX "/boards/mcu/rak11300/")
idf_component_register(
    SRCS ${SX126X_SRCS}
    INCLUDE_DIRS "src" "src/boards/mcu" "src/boards/mcu/espressif" "src/boards/sx126x" "src/mac" "src/mac/region" "src/radio" "src/radio/sx126x" "src/system" "src/system/crypto"
    REQUIRES arduino-esp32
)
