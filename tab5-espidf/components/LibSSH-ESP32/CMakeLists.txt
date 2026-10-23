# Vendored from github.com/ewpa/LibSSH-ESP32 @ 5.6.0 (matching
# ESP32-Bit-Pirate/platformio.ini's "ewpa/LibSSH-ESP32@^5.6.0" pin). Already
# ships as an ESP-IDF component (idf_component_register) upstream -- only
# change from the original is REQUIRES arduino -> arduino-esp32 to match
# this project's actual component name. Needed by
# lib/BitPirateCore/Services/SshService.h's unconditional
# <libssh/libssh.h>.
idf_component_register(
    SRC_DIRS "src" "src/threads" "src/external"
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-error=char-subscripts)
