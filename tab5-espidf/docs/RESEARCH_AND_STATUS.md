# Research conclusions, provenance and remaining work

**Snapshot:** 2026-10-29. This is a consolidated, publication-safe summary of the development/review sequence, not an independent hardware lab report. It deliberately excludes raw serial captures, MAC/SSID data, eFuse dumps, private machine paths and unverified forensic speculation. The detailed historical assessment material (chronological hardware-test logs, raw research dumps) is **not** included in this repository; it stays in private, unpublished archives outside version control.

## Resolved or materially narrowed

- **USB-A not bricked:** a minimal independent Device diagnostic and the Bit Pirate composite stack both enumerated through a USB-C hub; an earlier direct laptop connection failed. This locates a host/cable/topology-sensitive issue rather than proving Tab5 hardware destruction.
- **ESP-Hosted C6 reset polarity:** an incorrect reset-polarity Kconfig setting (`ACTIVE_LOW`, esp_hosted's own default for this board selection) was identified as a major SDIO bring-up regression and changed to `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y`; repeated reset-driven Wi-Fi/BLE tests passed, most recently re-confirmed in a fresh build/flash/CLI session on real hardware (clean boot, working `mode wifi`/`scan` and `mode bluetooth`/`scan`). True loss-of-all-power cold-boot qualification was not established by those tests.
- **USB power and ownership:** `USB5V_EN` must follow explicit USB role selection. Native HS OTG Device and the separate `Tab5UsbCdcService` Host implementation must not fight for the same controller; the service now owns `USB5V_EN` explicitly and fails transactionally, but remains inactive (unwired) in the main firmware.
- **Device CDC/HID/MSC:** composite enumeration observed and opt-in `sdusb` storage control implemented, with local/MSC ownership now guarded. Treat all forced failure cases and safe eject as needing qualification before unrestricted writable storage use.
- **Display/GPIO:** incorrect panel fallback was changed to explicit error; P4 GPIO range and reserved-pin checks improved with a second, no-escape `HARD_RESERVED_PINS` tier protecting the live C6 SDIO link from DIO bit-banging.
- **One-shot watchdog and input:** scheduler fixes applied to eight adapters; USB-UART verified in a limited run; the others have not each passed protocol/timing tests.
- **Managed component patches:** hash-gated, all-or-nothing (validate-all → copy-all → verify-all) vendor override procedure in `tools/apply_vendor_overrides.py`, guarded against `tools/vendor_overrides_manifest.json`; eight override files including an MSC teardown NULL guard. A clean build in this session exercised these checks successfully.

## Pending technical work

1. Real protocol-level S3 parity with flashrom, avrdude, OpenOCD, BPIO2, SUMP, wired buses and RF/IR modes.
2. Wi-Fi monitor/injection feasibility using custom C6 RPC rather than confusing stock ESP-Hosted API absence with silicon impossibility.
3. Resolving the one-UTMI-PHY conflict so `Tab5UsbCdcService` (P4 USB Host) can run without USB Device conflicts, then qualifying its role-aware resource/power lifecycle.
4. Failure-injection and host safe-eject tests for the SD/MSC ownership state machine.
5. Reliable and safe entry into P4 ROM download mode without undocumented eFuse changes; manual recovery procedure must remain available.
6. Complete GUI/input behavior mapping versus upstream S3/Web UI, plus upstream maintenance/merge plan.
7. GitHub Actions clean build, tagged artifact provenance, Web Flasher/M5Burner distribution and release/install testing.

Thermal comparisons and extended endurance were deliberately deferred. This document does not assert their completion.

## Provenance of the current `tab5-espidf/` state

- `tab5-espidf/` was added to this fork's `pioarduino` branch as a standalone Tab5 ESP-IDF port (see its introducing commit).
- A subsequent source-only fix pass (SDIO reset polarity, USB-A/`USB5V_EN` ownership, SD/MSC concurrency guard, two-tier DIO pin protection, hardened vendor-override application, and related adapter/controller/service corrections) was folded back into the same paths. No logs, generated images, or private assessment material were carried into this directory.
- This session performed a fresh `idf.py build` (ESP-IDF v5.5.4) against the result, flashed it to a real Tab5 unit over USB-C Serial/JTAG, and exercised the CLI, mode switching, `mode wifi`/`scan` and `mode bluetooth`/`scan` over the USB-A CDC port — all completed cleanly, confirming the SDIO reset-polarity fix in particular. This is a real hardware re-test of the current commit's working tree, not a historical report.

## How to publish / approach upstream

Keep this work in a Tab5-specific branch of the fork, retaining attribution and third-party licenses. Avoid rewriting geo-tp's official branch unilaterally. For upstream consideration, submit a clear architectural proposal plus a per-feature parity table, and explain why the LVGL/SIC/SGFX frontend preserves the command semantics while requiring distinct board/display integration. Further board-specific source changes should remain behind interfaces so shared core changes can be merged without a second permanent fork of Bit Pirate logic.

## References

- [geo-tp Bit Pirate](https://github.com/geo-tp/ESP32-Bit-Pirate) and [Tab5 PR #176](https://github.com/geo-tp/ESP32-Bit-Pirate/pull/176)
- [M5Stack Tab5 documentation and schematic](https://docs.m5stack.com/en/core/Tab5)
- [Espressif P4 hardware/USB guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html)
- [Espressif USB Device/Host topology](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_overview.html)
- [Espressif ESP-Hosted](https://github.com/espressif/esp-hosted-mcu)
