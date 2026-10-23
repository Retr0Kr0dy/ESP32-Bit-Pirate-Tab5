# Copied from ESP32-Bit-Pirate/lib/RF24/'s top-level files only (RF24.{h,cpp},
# RF24_config.h, nRF24L01.h, printf.h) -- the "utility/" subdirectory
# (Template/Teensy/pigpio/ATTiny) is NOT needed: RF24.cpp's own
# `#elif defined(ARDUINO) && !defined(RF24_SPI_TRANSACTIONS)` branch uses
# Arduino's SPI.h directly for any Arduino-family target including ESP32,
# confirmed by reading it -- those utility/ dirs are only for non-Arduino
# platforms (bare Linux/pigpio, bare Teensy, etc.). Needed by
# lib/BitPirateCore/Services/Rf24Service.h's unconditional <RF24.h>.
idf_component_register(
    SRCS "src/RF24.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
