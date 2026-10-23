# Copied verbatim from ESP32-Bit-Pirate/lib/EEPROM_SPI_WE/src/ (upstream's
# own vendored copy, not a registry fetch -- same as the PN532/Adafruit
# libraries). Needed by lib/BitPirateCore/Services/SpiService.h's
# unconditional <EEPROM_SPI_WE.h> (SPI mode).
idf_component_register(
    SRCS "src/EEPROM_SPI_WE.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
