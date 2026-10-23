# Vendored from github.com/adafruit/Adafruit-Si4713-Library @ 1.2.3
# (matching ESP32-Bit-Pirate/platformio.ini's "adafruit/Adafruit Si4713
# Library@^1.2.3" pin). Needed by
# lib/BitPirateCore/Services/FmService.h's unconditional
# <Adafruit_Si4713.h>. Depends on Adafruit_BusIO (I2C/SPI transport),
# already vendored in this project (components/Adafruit_BusIO) for PN532.
idf_component_register(
    SRCS "src/Adafruit_Si4713.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32 Adafruit_BusIO
)
