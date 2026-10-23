# Vendored from github.com/ESP32Async/AsyncTCP (main, unpinned upstream --
# ESP32-Bit-Pirate's platformio.ini doesn't list this directly, it's a
# transitive dependency eModbus's ModbusClientTCPasync.h/ModbusServerTCPasync.h
# need via their own unconditional <AsyncTCP.h>).
idf_component_register(
    SRCS "src/AsyncTCP.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
