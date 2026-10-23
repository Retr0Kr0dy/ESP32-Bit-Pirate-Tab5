# Copied from ESP32-Bit-Pirate/lib/SmartRC-CC1101-Driver-Lib/ (upstream's
# own vendored copy, not a registry fetch). Needed by
# lib/BitPirateCore/Services/SubGhzService.h's unconditional
# "ELECHOUSE_CC1101_SRC_DRV.h" (SubGHz mode). Shares the M-Bus dedicated
# SPI bus (SCK=5/MOSI=18/MISO=19) with SPI mode and RFID/I2C's bus, plus
# a dedicated GDO0 interrupt pin -- see main/CMakeLists.txt's
# SUBGHZ_GDO_PIN for which free pin.
idf_component_register(
    SRCS "src/ELECHOUSE_CC1101_SRC_DRV.cpp"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)

# setPA()'s PA_TABLE[1]=a path is genuinely reachable with 'a' uninitialized
# in a corner of the upstream switch (one of the 8dBm/10dBm cases falls
# through without assigning it before this toolchain's -O2 inlining makes
# that visible) -- pre-existing upstream code quality issue, not a P4-port
# bug, same class as the other vendored-lib warning-as-error suppressions
# in main/CMakeLists.txt.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=maybe-uninitialized)
