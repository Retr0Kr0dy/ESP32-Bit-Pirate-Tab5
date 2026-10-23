# Vendored from PlatformIO's registry CDN directly
# (dl.registry.platformio.org/download/gilman88/library/XModem/1.0.3/...),
# NOT from GitHub -- the gilman88/XModem GitHub repo referenced in
# ESP32-Bit-Pirate/platformio.ini ("gilman88/XModem@^1.0.3") no longer
# resolves there, but the exact pinned 1.0.3 tarball is still served from
# PlatformIO's own CDN. Needed by
# lib/BitPirateCore/Services/UartService.h's unconditional <XModem.h>
# (UART/HDUART modes).
idf_component_register(
    SRCS "src/XModem.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
