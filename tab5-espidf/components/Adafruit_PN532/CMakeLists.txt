# NOT fetched from GitHub -- copied verbatim from
# ESP32-Bit-Pirate/lib/PN532/Adafruit_PN532.{h,cpp}, the upstream project's
# own vendored (and modified: has a zero-arg Adafruit_PN532() default
# constructor that no real published adafruit/Adafruit-PN532 tag has ever
# shipped) copy, which is exactly what lib/BitPirateCore/Vendors/PN532.h
# was written against. Needed by that file (RFID mode), which includes
# <Adafruit_PN532.h> unconditionally.
idf_component_register(
    SRCS "src/Adafruit_PN532.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32 Adafruit_BusIO
)
