# Vendored from sparkfun/SparkFun_External_EEPROM_Arduino_Library @ v3.2.10
# (pinned to match ESP32-Bit-Pirate/platformio.ini's
# "sparkfun/SparkFun External EEPROM Arduino Library@^3.2.10").
# Needed by lib/BitPirateCore/Services/I2cService.h, which includes
# <SparkFun_External_EEPROM.h> unconditionally (I2C EEPROM read/write support).
idf_component_register(
    SRCS "src/SparkFun_External_EEPROM.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
