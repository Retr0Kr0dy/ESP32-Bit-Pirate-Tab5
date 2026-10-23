# Copied from ESP32-Bit-Pirate/lib/IRremote/src/ (upstream's own vendored
# copy of Arduino-IRremote, not a registry fetch). Header-only library
# (.hpp only, confirmed -- no SRCS). Needed by
# lib/BitPirateCore/Services/InfraredService.cpp's unconditional
# <IRremote.hpp> under the non-IRremoteESP8266 (Arduino-IRremote +
# INFRARED_RMT) backend this project uses -- see main/CMakeLists.txt's
# INFRARED_RMT define for why that backend was chosen (its RMT usage was
# already spike-tested OK on this exact chip in an earlier Phase-0 pass).
idf_component_register(
    INCLUDE_DIRS "src" "src/private"
    REQUIRES arduino-esp32
)
