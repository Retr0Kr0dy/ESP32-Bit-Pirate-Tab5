# Vendored from hideakitai/ESP32SPISlave @ v0.6.8 (the exact version
# ESP32-Bit-Pirate/platformio.ini pins). Header-only library -- no SRCS,
# just the include dir. Needed by
# lib/BitPirateCore/Services/SpiService.cpp's unconditional
# <ESP32SPISlave.h> (SPI slave/sniff support, SPI mode).
idf_component_register(
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
