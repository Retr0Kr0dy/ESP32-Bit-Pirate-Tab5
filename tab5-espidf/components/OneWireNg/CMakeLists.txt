function(target_optimize_size target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
            CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE -Os)
    endif()
endfunction()

if(IDF_VER)
    # ESP-IDF
    if(IDF_VERSION_MAJOR GREATER_EQUAL 5)
        idf_component_register(
            INCLUDE_DIRS
                src
            SRC_DIRS
                src
                src/platform
                src/drivers
            PRIV_REQUIRES
                driver
                esp_timer
                soc
        )
    else()
        idf_component_register(
            INCLUDE_DIRS
                src
            SRC_DIRS
                src
                src/platform
                src/drivers
        )
    endif()

    target_optimize_size(${COMPONENT_LIB})
elseif(PICO_SDK_VERSION_STRING)
    # Pico SDK
    file(GLOB target_src
        src/*.cpp
        src/platform/*.cpp
        src/drivers/*.cpp
    )
    add_library(OneWireNg STATIC ${target_src})

    target_include_directories(OneWireNg PUBLIC src)
    target_link_libraries(OneWireNg PUBLIC
        pico_stdlib
        hardware_pio
    )

    target_optimize_size(OneWireNg)
endif()

# Appended locally (not upstream): lib/BitPirateCore/Services/OneWireService.cpp
# calls OneWireNg::checkInvCrc16()/crc16(), both gated behind #if
# CONFIG_CRC16_ENABLED in OneWireNg.h/.cpp. ESP32-Bit-Pirate's own
# platformio.ini sets this exact flag ("-D CONFIG_CRC16_ENABLED=1 ; 1Wire
# EEPROM OneWireNg"). This component's PUBLIC compile definitions are what
# matter here, not main/CMakeLists.txt's -- that only reaches files built
# as part of the main component, not this separate component's own
# OneWireNg.cpp (where crc16() itself is implemented, confirmed by
# reading it directly -- without this, main links against a crc16() that
# was never compiled into this static library).
if(IDF_VER)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_CRC16_ENABLED=1)
endif()
