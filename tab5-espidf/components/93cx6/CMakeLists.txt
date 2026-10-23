# Vendored verbatim from ESP32-Bit-Pirate/lib/93cx6/93Cx6.{h,c}. Needed by
# lib/BitPirateCore/Services/ThreeWireService.h's unconditional "93Cx6.h"
# include (3-Wire/3WIRE mode). Already pure ESP-IDF (driver/gpio.h,
# freertos/*, esp_log.h) rather than Arduino -- no arduino-esp32 REQUIRES
# needed, unlike every other vendored protocol library in this project.
idf_component_register(
    SRCS "src/93Cx6.c"
    INCLUDE_DIRS "src"
    REQUIRES driver esp_driver_gpio
)
