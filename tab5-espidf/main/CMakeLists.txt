# ESP-IDF application: native Tab5 hardware drivers with upstream Bit Pirate
# command/protocol code from lib/BitPirateCore. SIC and SGFX are vendored
# native drivers. Only the required core source files are compiled here.

set(PROJ_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(SIC_DIR    ${PROJ_ROOT}/lib/SIC)
set(SGFX_DIR   ${PROJ_ROOT}/lib/SGFX)
set(BPCORE_DIR ${PROJ_ROOT}/lib/BitPirateCore)

foreach(d ${SIC_DIR} ${SGFX_DIR} ${BPCORE_DIR})
  if(NOT EXISTS ${d})
    message(FATAL_ERROR "Missing ${d} -- a vendored library/source tree is incomplete.")
  endif()
endforeach()

# ---- this project's own native-IDF + Arduino-task sources -----------------
set(APP_SRCS
  ${CMAKE_CURRENT_LIST_DIR}/main.c
  ${CMAKE_CURRENT_LIST_DIR}/display.c
  ${CMAKE_CURRENT_LIST_DIR}/touch_bridge.c
  ${CMAKE_CURRENT_LIST_DIR}/kbd_accessory.c   # real M5Stack Tab5 keyboard accessory, ported from Tab5-Orion
  ${CMAKE_CURRENT_LIST_DIR}/usb_device.c      # native esp_tinyusb HID+MSC device bridge, see its own header comment
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5TerminalView.cpp
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5DeviceView.cpp
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5TouchInput.cpp
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5Gui.cpp     # LVGL widget tree + display/touch driver bridge
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/WebUi.cpp          # Wi-Fi AP + serves the real web UI + its WebSocket
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5HostSerial.cpp # IHostSerial over usb_device.c's CDC -- USB adapter personas' host-facing transport
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5AiAssistant.cpp # "ai" command -- native port of the web UI's own Gemini AI panel
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5Screenshot.cpp # "screenshot" command -- debug tool, dumps the screen as base64 BMP over serial
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/Tab5UsbCdcService.cpp # Expander transport over USB-A host CDC (C5/CP210x dongle), see its header
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/fonts/lv_font_pirate_mono.c # real monospace glyph shapes (DejaVu Sans Mono) -- unscii_16's retro pixel shapes don't render the ASCII-art banner correctly
  ${CMAKE_CURRENT_LIST_DIR}/boards/tab5_bitpirate/BitPirateTask.cpp
)

# Bit Pirate source list. Keep the service/controller implementations
# upstream-identical wherever the ESP32-P4 does not require an adaptation.
set(BPCORE_SRCS
  ${BPCORE_DIR}/Services/PinService.cpp
  ${BPCORE_DIR}/Services/UtilityService.cpp
  ${BPCORE_DIR}/Services/I2sService.cpp
  ${BPCORE_DIR}/Transformers/ArgTransformer.cpp
  ${BPCORE_DIR}/Transformers/TerminalCommandTransformer.cpp
  ${BPCORE_DIR}/Transformers/PinoutTransformer.cpp
  ${BPCORE_DIR}/Transformers/InstructionTransformer.cpp
  ${BPCORE_DIR}/Managers/UserInputManager.cpp
  ${BPCORE_DIR}/Managers/CommandLineManager.cpp
  ${BPCORE_DIR}/Managers/CommandHistoryManager.cpp
  ${BPCORE_DIR}/Managers/AliasManager.cpp
  ${BPCORE_DIR}/Selectors/HorizontalSelector.cpp
  ${BPCORE_DIR}/Shells/HelpShell.cpp
  ${BPCORE_DIR}/Shells/I2cEepromShell.cpp
  ${BPCORE_DIR}/Shells/SmartCardShell.cpp
  ${BPCORE_DIR}/Analyzers/BinaryAnalyzer.cpp
  ${BPCORE_DIR}/Controllers/DioController.cpp
  ${BPCORE_DIR}/Controllers/I2cController.cpp
  ${BPCORE_DIR}/Controllers/TwoWireController.cpp
  ${BPCORE_DIR}/Services/I2cService.cpp
  ${BPCORE_DIR}/Services/I2cSnifferService.cpp
  ${BPCORE_DIR}/Services/TwoWireService.cpp
  ${BPCORE_DIR}/Analyzers/I2cFrequencyAnalyzer.cpp
  # ---- RFID (PN532, I2C-only) -- zero new pins: GlobalState defaults
  # rfidSda/SclPin from the same I2C_SDA_PIN/I2C_SCL_PIN macros I2C already
  # uses (Grove Port A), confirmed by reading GlobalState.h directly. ----
  ${BPCORE_DIR}/Vendors/PN532.cpp
  ${BPCORE_DIR}/Services/RfidService.cpp
  ${BPCORE_DIR}/Controllers/RfidController.cpp
  # ---- 3-Wire (93cx6 EEPROM, CS/SK/DI/DO pins -- 4 free M-Bus GPIO). ----
  ${BPCORE_DIR}/Services/ThreeWireService.cpp
  ${BPCORE_DIR}/Shells/ThreeWireEepromShell.cpp
  ${BPCORE_DIR}/Controllers/ThreeWireController.cpp
  # ---- 1-Wire (classic OneWire + OneWireNg for EEPROM ops). ----
  ${BPCORE_DIR}/Services/OneWireService.cpp
  ${BPCORE_DIR}/Shells/IbuttonShell.cpp
  ${BPCORE_DIR}/Shells/OneWireEepromShell.cpp
  ${BPCORE_DIR}/Controllers/OneWireController.cpp
  # ---- SPI (EEPROM_SPI_WE + SD card, M-Bus dedicated SPI bus). ----
  ${BPCORE_DIR}/Services/SpiService.cpp
  ${BPCORE_DIR}/Services/SdService.cpp
  ${BPCORE_DIR}/Shells/SdCardShell.cpp
  ${BPCORE_DIR}/Shells/SpiFlashShell.cpp
  ${BPCORE_DIR}/Shells/SpiEepromShell.cpp
  ${BPCORE_DIR}/Controllers/SpiController.cpp
  # ---- Infrared (Arduino-IRremote + direct RMT, see INFRARED_RMT below). ----
  ${BPCORE_DIR}/Vendors/MakeHex.cpp
  ${BPCORE_DIR}/Services/LittleFsService.cpp
  ${BPCORE_DIR}/Services/InfraredService.cpp
  ${BPCORE_DIR}/Transformers/InfraredRemoteTransformer.cpp
  ${BPCORE_DIR}/Transformers/SubGhzTransformer.cpp
  ${BPCORE_DIR}/Shells/UniversalRemoteShell.cpp
  ${BPCORE_DIR}/Controllers/InfraredController.cpp
  # ---- SubGHz (CC1101, shares M-Bus SPI bus + Grove C pins for CS/GDO0). ----
  ${BPCORE_DIR}/Services/SubGhzService.cpp
  ${BPCORE_DIR}/Analyzers/SubGhzAnalyzer.cpp
  ${BPCORE_DIR}/Controllers/SubGhzController.cpp
  # ---- RF24 (shares M-Bus SPI bus, CE/CSN reused from 3-Wire pins). ----
  ${BPCORE_DIR}/Services/Rf24Service.cpp
  ${BPCORE_DIR}/Controllers/Rf24Controller.cpp
  # Rf24Controller::handleJam() calls Vendors::beaconCreate() (WiFi beacon
  # spam, piggybacked onto RF24's jam command) -- needs wifi_atks.cpp's
  # real implementation (the header's inline no-op stub only applies if a
  # project-local "Vendors/WiFi.h" override exists, which ours doesn't).
  # Confirmed safe to add now: CONFIG_ESP_HOSTED_ENABLED=y in the current
  # sdkconfig (a concurrent pass's guarded fix, not touched here) means
  # WiFi.h/esp_wifi.h are genuinely available in this build.
  ${BPCORE_DIR}/Vendors/wifi_atks.cpp
  # ---- JTAG/SWD (JTAGulator-style pin scan, zero external library -- pure
  # Arduino digitalRead/digitalWrite bit-banging, confirmed by reading
  # Services/JtagService.h directly). ----
  ${BPCORE_DIR}/Services/JtagService.cpp
  ${BPCORE_DIR}/Controllers/JtagController.cpp
  # ---- USB: use the native P4 device-mode bridge in usb_device.c;
  # upstream UsbS3Service adapts its USB host + adapter-mode interface.
  ${BPCORE_DIR}/Services/NvsService.cpp
  ${BPCORE_DIR}/Services/SystemService.cpp
  ${BPCORE_DIR}/Shells/UsbAdapterShell.cpp
  ${BPCORE_DIR}/Shells/MouseShell.cpp
  ${BPCORE_DIR}/Services/UsbS3Service.cpp
  ${BPCORE_DIR}/Controllers/UsbS3Controller.cpp
  ${BPCORE_DIR}/Providers/DependencyProvider.cpp
  ${BPCORE_DIR}/Dispatchers/ActionDispatcher.cpp

  # ---- Real-dispatcher attempt: every remaining .cpp DependencyProvider's
  # full member list needs for a complete vtable/link, beyond what earlier
  # the first hardware bring-up. Headers
  # for all of these already proved to compile cleanly -- these were purely
  # missing .cpp additions, found via linker "undefined reference"/"vtable
  # for X" output, not missing dependencies. ----
  ${BPCORE_DIR}/Abstracts/ANetworkController.cpp
  ${BPCORE_DIR}/Controllers/BluetoothController.cpp
  ${BPCORE_DIR}/Services/BluetoothService.cpp
  ${BPCORE_DIR}/Controllers/CanController.cpp
  ${BPCORE_DIR}/Services/CanService.cpp
  ${BPCORE_DIR}/Shells/CellCallShell.cpp
  ${BPCORE_DIR}/Controllers/CellController.cpp
  ${BPCORE_DIR}/Shells/CellSmsShell.cpp
  ${BPCORE_DIR}/Services/CellService.cpp
  ${BPCORE_DIR}/Services/EthernetService.cpp
  ${BPCORE_DIR}/Controllers/EthernetController.cpp
  ${BPCORE_DIR}/Controllers/ExpanderController.cpp
  ${BPCORE_DIR}/Shells/FmBroadcastShell.cpp
  ${BPCORE_DIR}/Controllers/FmController.cpp
  ${BPCORE_DIR}/Services/FmService.cpp
  ${BPCORE_DIR}/Shells/GuideShell.cpp
  ${BPCORE_DIR}/Controllers/HdUartController.cpp
  ${BPCORE_DIR}/Services/HdUartService.cpp
  ${BPCORE_DIR}/Controllers/I2sController.cpp
  ${BPCORE_DIR}/Services/ICMPService.cpp
  ${BPCORE_DIR}/Controllers/LedController.cpp
  ${BPCORE_DIR}/Services/LedService.cpp
  ${BPCORE_DIR}/Controllers/LoRaController.cpp
  ${BPCORE_DIR}/Transformers/LoRaTransformer.cpp
  ${BPCORE_DIR}/Services/LoRaService.cpp
  ${BPCORE_DIR}/Shells/MeshtasticShell.cpp
  ${BPCORE_DIR}/Services/MeshtasticService.cpp
  ${BPCORE_DIR}/Services/ModbusService.cpp
  ${BPCORE_DIR}/Shells/ModbusShell.cpp
  ${BPCORE_DIR}/Services/NetcatService.cpp
  ${BPCORE_DIR}/Services/NmapService.cpp
  ${BPCORE_DIR}/Analyzers/PinAnalyzer.cpp
  ${BPCORE_DIR}/Shells/ProfileShell.cpp
  ${BPCORE_DIR}/Transformers/ProfileTransformer.cpp
  ${BPCORE_DIR}/Shells/SysInfoShell.cpp
  ${BPCORE_DIR}/Configurators/TerminalTypeConfigurator.cpp
  ${BPCORE_DIR}/Shells/UartAtShell.cpp
  ${BPCORE_DIR}/Controllers/UartController.cpp
  ${BPCORE_DIR}/Services/UartService.cpp
  ${BPCORE_DIR}/Services/UartSnifferService.cpp
  ${BPCORE_DIR}/Shells/UartEmulationShell.cpp
  ${BPCORE_DIR}/Controllers/UtilityController.cpp
  ${BPCORE_DIR}/Services/WifiService.cpp
  ${BPCORE_DIR}/Services/WifiOpenScannerService.cpp
  ${BPCORE_DIR}/Controllers/WifiController.cpp
  ${BPCORE_DIR}/Services/HttpService.cpp
  ${BPCORE_DIR}/Services/TelnetService.cpp
  ${BPCORE_DIR}/Services/SshService.cpp
  ${BPCORE_DIR}/Transformers/JsonTransformer.cpp
  ${BPCORE_DIR}/Transformers/AtTransformer.cpp
  # Captive-portal DNS for WebUi.cpp's AP -- see WebUi.cpp's own comment.
  ${BPCORE_DIR}/Servers/DnsServer.cpp
  # USB "adapter persona" one-shot boot path (UsbAdapterShell's "reboot into
  # X" menu). 8 of the original's 9 Adapters -- SdrCdcAdapter excluded
  # permanently, see BootModeConfigurator.cpp's own comment.
  ${BPCORE_DIR}/Configurators/BootModeConfigurator.cpp
  ${BPCORE_DIR}/Transformers/Bpio2Transformer.cpp   # Bpio2Adapter's own COBS encode/decode
  ${BPCORE_DIR}/Adapters/UsbUartBridgeAdapter.cpp
  ${BPCORE_DIR}/Adapters/FlashromSerprogAdapter.cpp
  ${BPCORE_DIR}/Adapters/AvrDudeBusPirateAdapter.cpp
  ${BPCORE_DIR}/Adapters/Bpio2Adapter.cpp
  ${BPCORE_DIR}/Adapters/SumpLogicAnalyzerAdapter.cpp
  ${BPCORE_DIR}/Adapters/OpenOcdBusPirateAdapter.cpp
  ${BPCORE_DIR}/Adapters/InfraredToyAdapter.cpp
  ${BPCORE_DIR}/Adapters/SubGhzRawCdcAdapter.cpp
)

# ---- SIC -----------------------------------------------------------------
# Same exclusions as Tab5-Launchpad/Tab5-Orion's own CMakeLists: drop the
# Arduino-only translation units and the duplicate dummy-driver registration.
file(GLOB_RECURSE SIC_SRCS ${SIC_DIR}/src/*.c)
list(FILTER SIC_SRCS EXCLUDE REGEX "_arduino\\.c$")
list(FILTER SIC_SRCS EXCLUDE REGEX "/backends/arduino/")
list(FILTER SIC_SRCS EXCLUDE REGEX "/src/bus/dummy\\.c$")

# ---- SGFX ------------------------------------------------------------------
# Tab5's only display transport is MIPI-DSI; drop the unreachable SPI/I2C
# HAL backends (legacy driver/i2c.h can't coexist at runtime with the new
# i2c_master driver everything else here uses) -- same reasoning/exclusion
# as Launchpad/Orion's own CMakeLists.
file(GLOB_RECURSE SGFX_SRCS ${SGFX_DIR}/src/*.c)
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/arduino/")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/stm32/")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/espidf/espidf_spi\\.c$")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/espidf/espidf_i2c\\.c$")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/sgfx_factory\\.c$")

idf_component_register(
  SRCS
    ${APP_SRCS}
    ${BPCORE_SRCS}
    ${SIC_SRCS}
    ${SGFX_SRCS}
  INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${SIC_DIR}/include
    ${SIC_DIR}/src            # board-private "boards/tab5/{ioexpander,panel_detect}.h"
    ${SGFX_DIR}/include
    ${BPCORE_DIR}             # lets the GUI classes and BitPirateTask #include
                               #   "Interfaces/...", "Enums/...", "Data/...",
                               #   "Models/...", "Controllers/...", etc. from BitPirateCore
  REQUIRES
    freertos
    driver
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_ledc
    esp_driver_rmt   # BitPirateTask's RMT RX spike (driver/rmt_rx.h, driver/rmt_tx.h)
    esp_driver_ppa   # display.c's orion_dsi_blit_rotated_ppa() -- hardware-accelerated
                     #   rotation blit, replacing a ~57ms CPU per-pixel loop
    esp_driver_sdmmc
    sdmmc
    esp_lcd
    esp_mm
    esp_psram
    esp_timer
    esp_hw_support
    spi_flash    # esp_flash.h   — SIC hal/hal_core_espidf.c
    fatfs        # esp_vfs_fat.h — SIC drivers/storage/sd_sdmmc_espidf.c
    nvs_flash
    espressif__esp_video         # pulled in only because SIC's autoreg couples the Tab5
                                 # camera driver to SIC_TARGET_TAB5+SIC_BACKEND_ESPIDF
    espressif__arduino-esp32    # BitPirateTask.cpp, Tab5*View/Input: Arduino.h, ESP_I2S.h,
                                 # esp32-hal-rmt.h, and everything lib/BitPirateCore needs
    SparkFun_External_EEPROM    # Services/I2cService.h's unconditional <SparkFun_External_EEPROM.h>
    Adafruit_PN532               # Vendors/PN532.h's unconditional <Adafruit_PN532.h> (RFID mode)
    93cx6                        # Services/ThreeWireService.h's unconditional "93Cx6.h" (3WIRE mode)
    OneWire                      # Services/OneWireService.h's unconditional <OneWire.h> (1WIRE mode)
    OneWireNg                    # same file's "OneWireNg_CurrentPlatform.h" (1WIRE EEPROM ops)
    EEPROM_SPI_WE                # Services/SpiService.h's unconditional <EEPROM_SPI_WE.h> (SPI mode)
    ESP32SPISlave                # Services/SpiService.cpp's unconditional <ESP32SPISlave.h> (SPI sniff/slave)
    IRremote                     # Services/InfraredService.cpp's unconditional <IRremote.hpp> (INFRARED_RMT backend)
    SmartRC-CC1101-Driver-Lib    # Services/SubGhzService.h's unconditional "ELECHOUSE_CC1101_SRC_DRV.h" (SubGHz)
    RF24                         # Services/Rf24Service.h's unconditional <RF24.h>
    lvgl__lvgl                  # Tab5Gui.cpp -- the real GUI's widget tree, see that file
    esp_http_server             # WebUi.cpp -- serves the real web UI + its WebSocket
    usb                         # Services/UsbS3Service.h's unconditional "usb/usb_host.h" (USB host mode)
    iot_usbh_cdc                # Tab5UsbCdcService.cpp -- Expander-over-USB-A host CDC client (components/iot_usbh_cdc)
    app_update                  # Services/SystemService.cpp's unconditional <esp_ota_ops.h>
    soc                          # Services/SystemService.cpp's unconditional "soc/rtc_cntl_reg.h"
    espressif__esp_tinyusb       # main/usb_device.c's native HID+MSC device bridge (Arduino's own
                                 # USB.h/USBHIDKeyboard.h never compiles real bodies on this
                                 # arduino-esp32/esp_tinyusb version combo -- see usb_device.h)
    FastLED                      # Services/LedService.h's unconditional <FastLED.h> (LED mode)
    mcp2515                      # Services/CanService.h's unconditional <mcp2515.h> (CAN mode)
    Adafruit_Si4713               # Services/FmService.h's unconditional <Adafruit_Si4713.h> (FM mode)
    XModem                        # Services/UartService.h's unconditional <XModem.h> (UART/HDUART modes)
    LibSSH-ESP32                  # Services/SshService.h's unconditional <libssh/libssh.h> (SSH mode)
    eModbus                       # Services/ModbusService.h's unconditional <ModbusClientTCPasync.h> (Modbus)
    AsyncTCP                      # eModbus's own transitive <AsyncTCP.h> dependency
    SX126x-Arduino                # Services/LoRaService.h's unconditional <SX126x-Arduino.h> (LoRa/Meshtastic)
    ArduinoJson                   # Transformers/JsonTransformer.h's unconditional <ArduinoJson.h>
  PRIV_REQUIRES
    vfs
)

# ---- Build-time configuration ----------------------------------------------
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  BITPIRATE_TAB5_VERSION="0.1.0"

  # ---- SIC: ESP-IDF backend, Tab5 board profile ----
  SIC_BACKEND_ESPIDF=1
  SIC_TARGET_TAB5=1

  # sic_battery_read() is a single global symbol; Tab5's implementation is
  # the INA226 at 0x41. Without this the link fails on an undefined
  # sic_battery_read.
  SIC_BATTERY_INA226=1

  # ---- SGFX: MIPI-DSI, runtime-selected panel ----
  SGFX_HAL_ESPIDF=1
  SGFX_DRV_ILI9881C=1
  SGFX_DRV_ST7121=1
  SGFX_DRV_ST7123=1

  # ---- GPIO defaults: external buses use Grove/M-Bus connectors,
  # not the Tab5 internal I2C bus (31/32) or SDIO/SDMMC lanes.
  I2C_SDA_PIN=53
  I2C_SCL_PIN=54
  I2C_FREQ=100000
  TWOWIRE_IO_PIN=17
  TWOWIRE_CLK_PIN=16
  TWOWIRE_RST_PIN=45

  # ---- 3-Wire (93cx6 EEPROM) -- 4 more free M-Bus GPIO. Pin tally so far
  # (M-Bus free list: 16,17,45,52,3,4,2,48,47,35,51 -- Grove A 53/54 and
  # Grove C 6/7 separate): I2C=53,54; 2WIRE=16,17,45; 3WIRE=3,4,2,52 (below)
  # -- remaining unclaimed after this: 48,47,35,51. ----
  THREEWIRE_CS_PIN=3
  THREEWIRE_SK_PIN=4
  THREEWIRE_DI_PIN=2
  THREEWIRE_DO_PIN=52

  # ---- 1-Wire -- single data pin, one more free M-Bus GPIO. Tally:
  # I2C=53,54; 2WIRE=16,17,45; 3WIRE=3,4,2,52; 1WIRE=48 (below) --
  # remaining unclaimed: 47,35,51. ----
  ONEWIRE_PIN=48

  # ---- SPI -- uses the M-Bus connector's DEDICATED SPI bus (SCK=5,
  # MOSI=18, MISO=19 -- NOT drawn from the generic free-GPIO pool used
  # above, that pool is down to only 47,35,51 left and SPI needs 4 pins).
  # Tab5DeviceView::getSharedSpiInstance() already targets these same 3
  # pins for the GUI's own SPI needs; SpiService configures the same
  # physical bus independently (raw pin numbers, not a shared SPIClass
  # instance) -- no real conflict, nothing else drives this bus
  # concurrently. CS from the free-GPIO pool: 47. Remaining unclaimed
  # after this: 35,51. ----
  SPI_CLK_PIN=5
  SPI_MISO_PIN=19
  SPI_MOSI_PIN=18
  SPI_CS_PIN=47

  # ---- Infrared -- last 2 free-GPIO-pool pins. Final tally: I2C=53,54;
  # 2WIRE=16,17,45; 3WIRE=3,4,2,52; 1WIRE=48; SPI=5,19,18,47 (M-Bus
  # dedicated SPI bus + 1 free pin); IR=35,51 -- free-GPIO pool
  # (16,17,45,52,3,4,2,48,47,35,51) now fully allocated, 0 remaining. ----
  IR_TX_PIN=35
  IR_RX_PIN=51

  # Backend: Arduino-IRremote + direct RMT HAL, NOT IRremoteESP8266 --
  # confirmed via InfraredService.h that INFRARED_RMT and
  # INFRARED_IREMOTE_ESP8266 are mutually exclusive (#error if both), and
  # this is the exact backend the Phase-0 spike already proved RMT
  # viability for on this chip (both the IDF-v5 RX API and the legacy
  # rmtInit()/rmtDeinit() wrapper InfraredService.cpp calls under this
  # flag). Matches ESP32-Bit-Pirate/platformio.ini's own
  # "-UINFRARED_IREMOTE_ESP8266 ; Use Arduino-IRremote as infrared lib".
  INFRARED_RMT=1

  # ---- SubGHz (CC1101) -- shares the M-Bus dedicated SPI bus (SCK=5,
  # MISO=19, MOSI=18, same physical pins SPI mode uses) plus its own CS
  # and GDO0 interrupt pin. The free-GPIO pool (16,17,45,52,3,4,2,48,47,
  # 35,51) is now fully allocated -- reusing Grove Port C's UART pins
  # (6,7) instead, since UART/HDUART mode is deferred (XModem dependency
  # unresolved) and isn't wired this session, so there's no real
  # conflict right now. If/when UART gets wired, these two pins will
  # need re-coordinating (same mutually-exclusive-mode pin-reuse pattern
  # the original S3 boards already use -- not a new problem introduced
  # here, just one more case of it). ----
  SUBGHZ_SCK_PIN=5
  SUBGHZ_SO_PIN=19
  SUBGHZ_SI_PIN=18
  SUBGHZ_CS_PIN=6
  SUBGHZ_GDO_PIN=7

  # ---- RF24 -- shares the M-Bus dedicated SPI bus (SCK=5/MISO=19/MOSI=18)
  # plus CE+CSN. The entire pin budget (free-GPIO pool + Grove C) is now
  # genuinely exhausted by earlier modes -- CE/CSN below deliberately
  # REUSE 3-Wire's DI/DO pins (2, 52). This is safe functionally (modes
  # are mutually exclusive; switching modes always reconfigures/releases
  # pins, confirmed by every *Controller::ensureConfigured()/
  # ensureReleased() pattern already used throughout this project) and is
  # the SAME pin-reuse-across-mutually-exclusive-modes approach the
  # original S3 boards already rely on -- but it DOES mean physically
  # wiring both an RF24 module and a 3-Wire EEPROM to this board
  # simultaneously isn't possible without separate wiring/switching, same
  # as the real hardware's own documented pin-reuse tradeoffs. ----
  RF24_SCK_PIN=5
  RF24_MISO_PIN=19
  RF24_MOSI_PIN=18
  RF24_CE_PIN=2
  RF24_CSN_PIN=52

  # ---- CAN (MCP2515, shares the M-Bus SPI bus via Arduino's default &SPI --
  # confirmed by reading CanService.h: `MCP2515 mcp2515 = MCP2515(CAN_CS_PIN, ...,
  # &SPI)`, no separate SCK/MISO/MOSI role to define). CS reuses 3-Wire's own
  # CS pin -- intentional, same mutually-exclusive-mode pin-reuse convention
  # already used throughout this file (RF24/CE reuses 3-Wire's DI, etc.), and
  # the same approach the original S3 project's own boards rely on. ----
  CAN_CS_PIN=3

  # ---- LED (FastLED, addressable LED strips) -- LED_DATA_PIN is a template
  # parameter (compile-time constant), confirmed by reading LedService.cpp's
  # FastLED.addLeds<CHIPSET, LED_DATA_PIN, ...>() calls. Reuses 1-Wire's pin --
  # same mutually-exclusive-mode convention as every other pin reuse above. ----
  LED_DATA_PIN=48

  # NOTE: CONFIG_CRC16_ENABLED=1 (needed by OneWireService's EEPROM CRC
  # checks) is defined in components/OneWireNg/CMakeLists.txt instead of
  # here -- it has to reach OneWireNg.cpp's own compilation (a separate
  # component from main), and that component's PUBLIC define already
  # propagates to main via REQUIRES, so defining it here too would be
  # redundant.

  # ---- Protected-pin list for DioController::isPinAllowed() (GlobalState.h
  # parses this into state.getProtectedPins()). Was previously undefined on
  # this target -- DIO mode had zero protection against a user targeting a
  # pin already committed to internal use. Derived from lib/SIC/docs/PINS_TAB5.md
  # + lib/SIC/src/boards/tab5/ioexpander.h (both read directly, not from memory):
  #   0,1    -- keyboard-accessory I2C (TAB5_KBD_I2C_SDA/SCL)
  #   8-13,15 -- SDIO to the ESP32-C6 co-processor (CLK/CMD/D0-D3) + C6 RST
  #   22     -- backlight PWM
  #   23     -- touch INT
  #   26-30  -- I2S0 audio (DOUT/BCLK/DIN/WS/MCLK)
  #   31,32  -- internal I2C bus 0 (touch/IMU/RTC/charger/panel-id) -- the
  #             specific pair this fix exists to make sure DIO can't touch
  #   36     -- camera XCLK
  #   39-44  -- SD card SDMMC (D0-D3/CLK/CMD)
  # Deliberately NOT included: Grove A (53,54), Grove B/M-Bus free GPIO
  # (16,17,45,52,3,4,2,48,47,35,51), Grove C (6,7) -- all legitimately free
  # and some already wired above; protecting those would break I2C/2WIRE.
  "PROTECTED_PINS=\"0,1,8,9,10,11,12,13,15,22,23,26,27,28,29,30,31,32,36,39,40,41,42,43,44\""

  # ---- DioController.cpp's shared isPinAllowed() hardcodes "pin > 48 /*
  # max pin for S3 */" with no per-board override point. 48 is wrong for
  # ESP32-P4, which has legitimately free, already-wired pins above that
  # (Grove A's I2C_SCL_PIN=54 above; M-Bus free GPIO 51/52) -- that check
  # would otherwise silently reject them. See the matching #ifdef added at
  # DioController.cpp's isPinAllowed(). 54 covers every pin this target
  # actually uses or documents as free. ----
  DIO_MAX_PIN=54

  # ---- JTAG/SWD (GlobalState.h's jtagScanPins default, {1,3,5,7,9} upstream
  # -- pin 1 is reserved on Tab5, so that default is unsafe here without an
  # override). JtagController is a JTAGulator-style scanner, not a fixed-pin
  # protocol: state.getJtagScanPins() is just the starting suggestion shown
  # by 'config', reconfigurable at runtime and re-validated against
  # PROTECTED_PINS above either way. Deliberately reuses already-wired M-Bus
  # free GPIO (16,17,45,52,3) rather than claiming new dedicated pins --
  # JTAG/SWD scanning is inherently exploratory (probes whatever external
  # target you clip onto), same mutually-exclusive-pin-reuse pattern as
  # RF24/3-Wire above, and these are genuinely free per PINS_TAB5.md either way.
  "JTAG_SCAN_PINS=\"16,17,45,52,3\""

  # ---- Default-pin audit (2026-10-21): now that the real ActionDispatcher/
  # DependencyProvider is the live dispatcher, every mode upstream ships is
  # actually reachable -- including ones whose hardcoded GlobalState.h
  # defaults were chosen for an S3 board's reserved-pin set, not Tab5's.
  # LoRaController's default DIO1=1 (a protected pin, no escape from the
  # reprompt loop) surfaced this as a real, confirmed bug; this is the
  # systematic fix for every other mode with the same latent problem,
  # found by checking every GlobalState.h default against PROTECTED_PINS
  # above. All values below reuse the same free M-Bus/Grove pool, same
  # mutually-exclusive-mode pin-reuse convention as every block above --
  # not new dedicated pins, same tradeoff already documented throughout
  # this file.
  #
  # UART: default uartRxPin=1 is protected (uartTxPin=2 is already fine).
  UART_RX_PIN=37
  # HDUART: default hdUartPin=1 (single-wire half-duplex) is protected.
  HDUART_PIN=38
  # I2S: all three defaults (41,43,42) are protected.
  I2S_BCLK_PIN=33
  I2S_LRCK_PIN=34
  I2S_DATA_PIN=20
  # CAN: default canSckPin=0 is protected, AND default canSoPin=3 silently
  # collided with CAN_CS_PIN=3 above (two different roles on one pin within
  # the same mode -- a real bug independent of the protected-pin issue).
  CAN_SCK_PIN=24
  CAN_SO_PIN=25
  # Ethernet: default ethernetMosiPin=23 and ethernetIrqPin=39 are protected
  # (CS=5/RST=4/SCK=18/MISO=19 defaults are already fine, left as-is).
  ETHERNET_MOSI_PIN=33
  ETHERNET_IRQ_PIN=34
  # LoRa: GlobalState.h gates all seven LoRa pins behind one combined
  # "#ifdef LORA_SCK_PIN" block (loRaSckPin/MisoPin/MosiPin/CsPin/RstPin/
  # BusyPin/Dio1Pin all set together) -- confirmed by reading the actual
  # header, not assumed -- so all seven must be defined here even though
  # only Dio1 (default 1) is actually protected; the other six defaults
  # (5,6,7,4,3,2) are already safe and are simply restated at their
  # existing values.
  LORA_SCK_PIN=5
  LORA_MISO_PIN=6
  LORA_MOSI_PIN=7
  LORA_CS_PIN=4
  LORA_RST_PIN=3
  LORA_BUSY_PIN=2
  LORA_DIO1_PIN=24
)

# Vendored third-party sources: keep their warnings out of the way of ours.
set_source_files_properties(${SIC_SRCS} ${SGFX_SRCS}
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-function")

# I2sService.cpp:167 -- "1 << (bitsPerSample) - 1" trips -Wparentheses (IDF
# promotes it to an error via -Wall). The expression is correct as written
# (- binds tighter than <<, so this is 1 << (bitsPerSample - 1)) but it's
# unmodified upstream Bit Pirate source, not ours to patch here -- same
# reasoning/pattern as the esp-modbus stringop-truncation suppression above.
set_source_files_properties(${BPCORE_DIR}/Services/I2sService.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=parentheses")

# More of the same -- unmodified upstream Bit Pirate source hitting
# -Werror on this toolchain, not ours to patch:
#  - SmartCardShell/BinaryAnalyzer: constructor member-init list order
#    doesn't match declaration order (harmless, -Werror=reorder).
#  - BinaryAnalyzer: %u against a size_t whose underlying type is "long
#    unsigned int" on this riscv32 toolchain's ABI, not "unsigned int" --
#    same width, different typedef, harmless (-Werror=format=).
#  - I2cEepromShell: compares a kActions[index] (const char*) against a
#    string literal by pointer identity rather than content
#    (-Werror=address) -- a pre-existing upstream quirk, not a P4-port bug.
set_source_files_properties(${BPCORE_DIR}/Shells/SmartCardShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
set_source_files_properties(${BPCORE_DIR}/Analyzers/BinaryAnalyzer.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder;-Wno-error=format")
set_source_files_properties(${BPCORE_DIR}/Shells/I2cEepromShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=address")
#  - ThreeWireController: same constructor member-init-order issue as
#    SmartCardShell/BinaryAnalyzer above (-Werror=reorder).
set_source_files_properties(${BPCORE_DIR}/Controllers/ThreeWireController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - OneWireEepromShell: same reorder issue, plus the same emoji-string
#    pointer-identity comparison as I2cEepromShell above (-Werror=address).
set_source_files_properties(${BPCORE_DIR}/Shells/OneWireEepromShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder;-Wno-error=address")
#  - SdCardShell/SpiFlashShell/SpiEepromShell: same reorder issue.
set_source_files_properties(${BPCORE_DIR}/Shells/SdCardShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - SpiFlashShell/SpiEepromShell: reorder + the same emoji-string
#    pointer-identity comparison as I2cEepromShell/OneWireEepromShell.
set_source_files_properties(
    ${BPCORE_DIR}/Shells/SpiFlashShell.cpp
    ${BPCORE_DIR}/Shells/SpiEepromShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder;-Wno-error=address")
#  - MakeHex (Vendors/, used by Infrared): same reorder issue (IRP::IRP()).
set_source_files_properties(${BPCORE_DIR}/Vendors/MakeHex.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - InfraredController: same reorder issue.
set_source_files_properties(${BPCORE_DIR}/Controllers/InfraredController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - DependencyProvider: same reorder issue, at real-dispatcher-construction
#    scale (~40 members initialized in a different order than declared) --
#    same class of pre-existing cosmetic upstream issue as every other
#    -Wno-error=reorder suppression above, not a P4-port bug.
set_source_files_properties(${BPCORE_DIR}/Providers/DependencyProvider.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - ActionDispatcher: switch over ModeEnum doesn't handle the sentinel
#    None/COUNT values (never real runtime modes) -- same class of
#    pre-existing cosmetic upstream issue, not a P4-port bug.
set_source_files_properties(${BPCORE_DIR}/Dispatchers/ActionDispatcher.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=switch")
#  - ANetworkController/CanController: same reorder issue.
set_source_files_properties(${BPCORE_DIR}/Abstracts/ANetworkController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
set_source_files_properties(${BPCORE_DIR}/Controllers/CanController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - CanService: %X against a canid_t whose underlying type is "long
#    unsigned int" on this toolchain -- same ABI-typedef mismatch class
#    as other vendored/upstream format suppressions in this file.
set_source_files_properties(${BPCORE_DIR}/Services/CanService.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=format")
#  - CellCallShell/FmBroadcastShell: comparing a const char* against a
#    string literal by pointer identity -- same class as the earlier
#    I2cEepromShell -Wno-error=address suppression.
set_source_files_properties(${BPCORE_DIR}/Shells/CellCallShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=address")
set_source_files_properties(${BPCORE_DIR}/Shells/FmBroadcastShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=address")
#  - LedService: memset() on a non-trivial CRGB struct -- harmless in
#    practice (CRGB is a plain 3-byte-struct-like type with no vtable/
#    non-trivial members despite GCC's class-memaccess heuristic flagging
#    it), same class of pre-existing upstream code-quality warning as
#    every other suppression in this file.
set_source_files_properties(${BPCORE_DIR}/Services/LedService.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=class-memaccess")
#  - UartController/UtilityController/NmapService/ModbusShell: same
#    reorder issue as every other suppression above.
set_source_files_properties(${BPCORE_DIR}/Controllers/UartController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
set_source_files_properties(${BPCORE_DIR}/Controllers/UtilityController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
set_source_files_properties(${BPCORE_DIR}/Services/NmapService.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
set_source_files_properties(${BPCORE_DIR}/Shells/ModbusShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=reorder")
#  - UartEmulationShell: snprintf-style truncation warning on a
#    comfortably-bounded buffer -- same class of harmless pre-existing
#    upstream warning as the rest of this file's suppressions.
set_source_files_properties(${BPCORE_DIR}/Shells/UartEmulationShell.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=format-truncation")
#  - SubGhzAnalyzer: two "if (x) y; if (a) b;" statements on one line
#    (decodeModulationRSSI()) trip -Werror=misleading-indentation --
#    genuinely harmless (both ifs have their own single-statement body,
#    just share a line), pre-existing upstream formatting, not a bug.
set_source_files_properties(${BPCORE_DIR}/Analyzers/SubGhzAnalyzer.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=misleading-indentation")
#  - wifi_atks.cpp (Vendors/, RF24's handleJam() dependency): uses the
#    long-removed FreeRTOS macro portTICK_RATE_MS (renamed
#    portTICK_PERIOD_MS upstream in IDF's FreeRTOS years ago) -- a real
#    compile error, not a warning, so an alias define instead of a
#    -Wno-error suppression.
set_source_files_properties(${BPCORE_DIR}/Vendors/wifi_atks.cpp
  PROPERTIES
    COMPILE_DEFINITIONS "portTICK_RATE_MS=portTICK_PERIOD_MS"
    COMPILE_OPTIONS "-Wno-error=stringop-truncation")
#  - JtagController: "0x%08X" against a std::vector<unsigned long> element --
#    same "long unsigned int" vs "unsigned int" ABI mismatch as
#    BinaryAnalyzer above (-Werror=format=), not a real bug.
set_source_files_properties(${BPCORE_DIR}/Controllers/JtagController.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error=format")
