# ESP32 Bit Pirate — M5Stack Tab5 (ESP32-P4 + ESP32-C6)
# Standalone ESP-IDF 5.5.4 firmware project. The Bit Pirate command/adapter
# core is included under lib/BitPirateCore; P4 board support, LVGL and native
# USB are implemented in main/ and the SIC/SGFX libraries.
# Locked managed-component overrides are applied during project configure;
# see tools/apply_vendor_overrides.py and docs/BUILD_AND_RELEASE.md.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-bit-pirate-p4-tab5)

# Several managed_components/ sources don't build or boot correctly as
# shipped on ESP32-P4/Tab5 (see tools/apply_vendor_overrides.py's own
# header for the full list). Re-applied every configure, since the
# component manager overwrites managed_components/ on each fetch.
execute_process(
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/apply_vendor_overrides.py
    COMMAND_ERROR_IS_FATAL ANY
)

# arduino-esp32 (main/idf_component.yml) pulls espressif__esp-modbus in as
# one of its own transitive dependencies -- this firmware has no Modbus
# mode (yet; ESP32-Bit-Pirate/src/Services/ModbusService.h exists upstream
# for a later phase, but nothing wires it in today). Its vendored
# mb_ports/tcp/port_tcp_utils.c trips -Wstringop-truncation, which IDF
# promotes to an error via -Wall by default. It's not our source to patch
# (component-manager-fetched, overwritten on dependency resolution), so
# suppress just this one diagnostic on just this one managed component
# rather than weakening -Werror project-wide.
idf_component_get_property(modbus_lib espressif__esp-modbus COMPONENT_LIB)
if(modbus_lib)
    target_compile_options(${modbus_lib} PRIVATE -Wno-error=stringop-truncation)
endif()

# arduino-esp32 3.3.12's libraries/ESP_Video/src/ESP_Video.cpp (its Camera
# wrapper) is written against a newer espressif/esp_video API
# (esp_video_init_with_flags, ESP_VIDEO_INIT_FLAGS_*, a dont_init_ldo field)
# than the 0.7.0 this project pins -- inherited from Tab5-Launchpad/SIC's
# camera-autoreg coupling (SIC_TARGET_TAB5 unconditionally pulls esp_video
# in; see main/CMakeLists.txt). Upgrading esp_video instead would mean
# re-validating SIC's whole camera bring-up against a new API shape for a
# camera this firmware never uses -- not worth it. arduino-esp32's own
# ARDUINO_SELECTIVE_ESP_Video Kconfig option would normally exclude this
# file, but turning on CONFIG_ARDUINO_SELECTIVE_COMPILATION breaks ESP_SR
# instead: ESP_I2S is one of arduino-esp32's ARDUINO_ALL_LIBRARIES but has
# no matching Kconfig symbol of its own (upstream gap, confirmed against
# Kconfig.projbuild), so selective mode silently drops ESP_I2S's sources
# and ESP_SR's "#include ESP_I2S.h" stops resolving. So: leave selective
# compilation off (full default library set, which is what actually
# builds cleanly) and instead excise just the one broken translation unit
# directly, the same way a .gitignore'd generated file would be skipped.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/managed_components/espressif__arduino-esp32/libraries/ESP_Video/src/ESP_Video.cpp
    DIRECTORY ${CMAKE_SOURCE_DIR}/managed_components/espressif__arduino-esp32
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# arduino-esp32's own BLE sources (esp32-hal-bt.c, esp32-hal-misc.c,
# libraries/BLE/src/BLEDevice.h, etc.) gate on plain CONFIG_NIMBLE_ENABLED /
# CONFIG_BLUEDROID_ENABLED (no BT_ prefix) -- but the real ESP-IDF Kconfig
# symbols for the bt component are CONFIG_BT_NIMBLE_ENABLED /
# CONFIG_BT_BLUEDROID_ENABLED (confirmed against esp-idf/components/bt/Kconfig
# directly; no unprefixed alias exists anywhere in this dependency tree).
# Bridge the naming gap with a plain compile definition rather than patching
# arduino-esp32's vendored sources in many places -- applied to its own
# component (so its own BLE/HAL sources see it) and to main (so
# BluetoothService.h's own #include <BLEDevice.h> sees it too).
idf_component_get_property(arduino_lib espressif__arduino-esp32 COMPONENT_LIB)
if(arduino_lib AND CONFIG_BT_NIMBLE_ENABLED)
    target_compile_definitions(${arduino_lib} PUBLIC NIMBLE_ENABLED=1)
endif()

# ssl_client_stub (components/ssl_client_stub/) provides stub start_ssl_client/
# stop_ssl_socket/etc. symbols NetworkClientSecure.cpp needs (its real
# ssl_client.cpp compiles to zero symbols under this project's mbedTLS
# config). A plain REQUIRES on arduino-esp32 can't be added without
# patching its own CMakeLists.txt (overwritten on every component-manager
# fetch); linking it directly onto the already-built component target
# here gets CMake's own dependency graph to place it correctly instead.
idf_component_get_property(sslstub_lib ssl_client_stub COMPONENT_LIB)
if(arduino_lib AND sslstub_lib)
    target_link_libraries(${arduino_lib} PRIVATE ${sslstub_lib})
endif()
