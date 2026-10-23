# Vendored from paulstoffregen/OneWire @ v2.3.8 (the exact version
# ESP32-Bit-Pirate/platformio.ini pins). AVR-only code paths (util/crc16.h,
# direct register I/O) are already guarded by #if defined(__AVR__) in the
# upstream source -- confirmed by reading it -- so this builds fine for
# ESP32-family targets via its existing ARDUINO_ARCH_ESP32 code path.
# Needed by lib/BitPirateCore/Services/OneWireService.h's unconditional
# <OneWire.h> (1-Wire mode).
#
# PATCHED locally: src/util/OneWire_direct_gpio.h's ARDUINO_ARCH_ESP32 fast
# -GPIO path had only two sub-branches -- CONFIG_IDF_TARGET_ESP32C3 (single
# GPIO register, .val member access) and a "plain ESP32" fallback (flat
# uint32_t register fields, two registers split at pin 32). Neither fits
# P4: it has >32 GPIOs (needs the two-register split, unlike C3) AND its
# gpio_struct.h generates real struct/union registers with a .val member
# (unlike classic ESP32's flat fields) -- confirmed by reading
# esp-idf/components/soc/esp32p4/register/hw_ver1/soc/gpio_struct.h
# directly. Added a dedicated CONFIG_IDF_TARGET_ESP32P4 branch per function
# (directRead/directWriteLow/directWriteHigh/directModeInput/
# directModeOutput) combining both: two-register split + .val access,
# pin range widened to <54 (P4's real GPIO count). Also deliberately did
# NOT copy classic ESP32's "pin <= 33" output-only ceiling in
# directModeOutput (that's ESP32's own strapping/ADC-pin restriction, not
# P4's -- this project already drives real outputs above pin 33 on P4
# elsewhere) or the IDF<4 RTC-pull dead code.
idf_component_register(
    SRCS "src/OneWire.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
