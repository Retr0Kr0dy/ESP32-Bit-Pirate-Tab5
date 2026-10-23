# Vendored from github.com/FastLED/FastLED @ 3.10.3 (matching
# ESP32-Bit-Pirate/platformio.ini's lib_deps pin). Needed by
# lib/BitPirateCore/Services/LedService.h's unconditional <FastLED.h>.
#
# FastLED's RMT5 ESP32 backend (platforms/esp/32/rmt_5/strip_rmt.cpp) calls
# led_strip_refresh_async()/led_strip_refresh_wait_done(), which only exist
# in FastLED's OWN bundled fork of led_strip under
# src/third_party/espressif/led_strip/src (*.c) -- NOT in the upstream
# espressif/led_strip IDF component (confirmed: upstream 2.5.5 only has the
# synchronous led_strip_refresh()). Must glob *.c too, and must NOT also
# depend on the real espressif__led_strip component, since that would give
# two different definitions of the same global C symbols (led_strip_refresh,
# led_strip_new_rmt_device, etc.) and fail to link with duplicate symbols.
file(GLOB_RECURSE FASTLED_SRCS "src/*.cpp" "src/*.c")
idf_component_register(
    SRCS ${FASTLED_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
