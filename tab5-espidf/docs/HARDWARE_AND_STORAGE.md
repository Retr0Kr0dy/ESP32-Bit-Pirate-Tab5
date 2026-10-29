# Tab5 architecture, pins, USB and SD ownership

## Architecture

The ESP32-P4 owns display/touch, native USB and external peripheral interfaces. The onboard ESP32-C6 supplies Wi-Fi/BLE through ESP-Hosted over a dedicated SDIO transport and reset/power-control arrangement. The frontend is LVGL with SIC/SGFX rather than the official M5Unified GUI. Core command/controller/adapter logic is ported under `lib/BitPirateCore`.

Important source entry points: `main/main.c`, `main/usb_device.c`, `main/boards/tab5_bitpirate/BitPirateTask.cpp`, `lib/SIC/src/boards/tab5/ioexpander.c`, `lib/SIC/src/drivers/storage/sd_sdmmc_espidf.c`, `lib/BitPirateCore/Services/UsbS3Service.cpp`.

## ESP-Hosted SDIO reset polarity

`sdkconfig.defaults` forces `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y`, overriding esp_hosted's own `ACTIVE_LOW` default for the `ESP32P4_TAB5_C6_BOARD` selection. On this unit, `ACTIVE_LOW` reliably fails SDIO bring-up to the C6 (the transport never reaches `TRANSPORT_TX_ACTIVE`); `ACTIVE_HIGH` brings it up cleanly and has since passed repeated reset-driven Wi-Fi/BLE cycles. True loss-of-all-power cold-boot qualification (as opposed to a software/CPU reset) has not been separately established. If the hosted link ever fails to come up after an upstream `dependencies.lock` bump of `espressif__esp_hosted`, check whether this default changed before assuming a hardware fault.

## USB role / power boundaries

| Path | Typical use | Limitations |
|---|---|---|
| USB-C | USB Serial/JTAG boot, console, flash | Fixed-function controller; not interchangeable with the native TinyUSB composite stack |
| USB-A | P4 High-Speed USB OTG, experimental Device: CDC/HID/MSC | The 5 V output (`USB5V_EN`) must be **off** for Device role, and Host and Device cannot simultaneously own the same HS controller/PHY |
| USB-A Host (`Tab5UsbCdcService`, wired in but disabled by default) | Powered external USB peripheral (e.g. a C5/CP210x devkit) | `usb_host_install()`'s pump task starves `IDLE0`/CPU0 forever while the native device-mode stack also holds the one UTMI PHY; the service itself now owns `USB5V_EN` explicitly and fails transactionally (see its own header comment), but stays unwired pending a resolution to the PHY conflict |

The Tab5's IO expander at **0x44**, `E2.P3`, controls `USB5V_EN`; `E2.P0` controls C6 `WLAN_PWR_EN` (M5Stack hardware documentation). `ioexpander.c`'s boot sequence leaves `USB5V_EN` **off** by default (this port runs USB-A as a TinyUSB *device*, which must never source VBUS onto a host's own VBUS pin); `Tab5UsbCdcService::begin()` is the one legitimate exception, since it is an actual USB host and must explicitly enable the rail itself. Do not connect host-to-host USB-A cables or test backfeeding: verify VBUS polarity with a meter before introducing a new Device-mode connection. USB-A has been observed to work through a USB-C hub in cases where a direct host-side path failed; that does **not** by itself prove an electrically damaged Tab5 or laptop port — treat it as a host/cable/topology-sensitive finding, not a hardware verdict.

Relevant silicon USB controllers: P4 has USB-Serial/JTAG, FS OTG and HS OTG, with different default pins and PHY allocation. Do not program eFuses simply to make a USB routing experiment work.

## microSD and USB MSC

The built-in microSD card uses the Tab5 SDMMC path (GPIO39–44 per M5Stack documentation; see the current SIC pin map for actual configuration). Local application mounting and USB Mass Storage access **must be mutually exclusive**. The Cardputer/S3 `SD.readRAW`/`writeRAW` callbacks are not evidence simultaneous host + local filesystem writes are safe.

A mutex-protected ownership guard and `sdusb on/off/status` make MSC optional instead of consuming the card at every USB startup. Source paths: `lib/SIC/src/drivers/storage/sd_sdmmc_espidf.c`, `main/usb_device.c`, `lib/BitPirateCore/Services/UsbS3Service.cpp`. These are reviewed corrective changes; failure-injection and every host safe-eject case have **not** been hardware-qualified.

Safe workflow: close application files, stop local access, enable MSC, mount/use on PC, **eject/unmount from the PC**, turn `sdusb off`, then remount locally. Never use an irreplaceable card for unsafe removal or error injection. If TinyUSB teardown fails, treat the device as requiring controlled reboot rather than re-claiming the card while a USB stack may still run.

## Reserved pins and electrical limits

DIO mode checks pins against two tiers, not one: a confirmable `PROTECTED_PINS` list (`DioController::isPinAllowed()` warns and lets the user proceed anyway, since using one of these just loses that one peripheral until reboot) and a stricter `HARD_RESERVED_PINS` list with no escape at all — specifically the live SDIO link to the C6 (CLK/CMD/D0-D3 + reset), since bit-banging any of those via DIO can glitch an in-flight SDIO transaction rather than merely losing a peripheral.

The P4 exposes more GPIO indices than the S3; shared code must use the board's `DIO_MAX_PIN` and the hard-reserved pin list above, rather than hardcoded S3 GPIO48 assumptions. Never reassign internal C6 transport, display, touch, SDMMC or IO-expander pins to general-purpose modes. See `lib/SIC/docs/PINS_TAB5.md` and the actual board implementation for the current list. Observe 3.3 V signal-level and peripheral-specific current limits; a 5 V labeled connector supply is not permission to put 5 V on P4 GPIO.

## Primary references

- [M5Stack Tab5 hardware and schematic resources](https://docs.m5stack.com/en/core/Tab5)
- [Espressif ESP32-P4 USB controller overview](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_overview.html)
- [Espressif USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- [ESP32-P4 USB hardware checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html)
- [ESP-Hosted project](https://github.com/espressif/esp-hosted-mcu)
