# Vendored from github.com/eModbus/eModbus @ v1.7.4.stable (matching
# ESP32-Bit-Pirate/platformio.ini's "miq19/eModbus@^1.7.4" pin). Needed by
# lib/BitPirateCore/Services/ModbusService.h's unconditional
# <ModbusClientTCPasync.h>.
file(GLOB EMODBUS_SRCS "src/*.cpp")
idf_component_register(
    SRCS ${EMODBUS_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32 AsyncTCP
)

# Same class of harmless uint32_t/%u ABI-typedef mismatch (uint32_t is
# "long unsigned int" on this toolchain) as other vendored-lib warning
# suppressions elsewhere in this project -- pre-existing upstream code,
# not a P4-port bug.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)
