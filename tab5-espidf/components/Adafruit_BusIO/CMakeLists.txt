# NOT fetched from GitHub -- the upstream ESP32-Bit-Pirate project already
# vendors its own modified copy of this (plus Adafruit_PN532) directly in
# ESP32-Bit-Pirate/lib/PN532/, matching exactly what
# lib/BitPirateCore/Vendors/PN532.h was written against (confirmed: the
# real github.com/adafruit/Adafruit_BusIO never shipped a zero-argument
# Adafruit_PN532() constructor at any tagged version -- upstream's own
# fork is the one with it). Copied verbatim from there, not re-fetched.
idf_component_register(
    SRCS
        "src/Adafruit_I2CDevice.cpp"
        "src/Adafruit_SPIDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
