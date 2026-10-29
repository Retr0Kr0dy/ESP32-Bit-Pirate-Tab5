# ESP32-S3 vs Tab5 feature parity checklist

This table separates **implemented**, **evidence of working on one unit**, and **fully qualified**. An existing source implementation or CLI menu entry is not a passing protocol test. The underlying hardware investigation used one Tab5 unit and a limited set of trials, most recently re-confirmed with a clean `idf.py build` + flash + on-device CLI/Wi-Fi-scan/BLE-scan session on 2026-10-29. **No claim of complete S3 parity is made here.**

| Capability | Tab5 implementation / evidence | Outstanding acceptance test |
|---|---|---|
| Boot/display/LVGL/touch | Port boots and displays GUI | Touch/key combinations, rotation, long sequences |
| USB-A HS Device / CDC | Enumerated at 480 Mbps; CLI usable | Multi-host connect/disconnect and role switching |
| USB HID keyboard/mouse/gamepad | Composite interface observed | Execute actual reports on real host, all modes |
| USB MSC / microSD | Host sees disk; opt-in `sdusb` implementation | Safe eject, failure injection, card integrity, ownership under concurrency |
| USB Host CDC (`Tab5UsbCdcService`) | Hardened (explicit `USB5V_EN`, transactional begin/stop) but stays unwired by default | Resolve the one-UTMI-PHY conflict with the active Device stack before enabling |
| USB-C console / flashing | USB Serial/JTAG operation established | Automatic `usbboot`/ROM entry **not qualified** |
| Wi-Fi via C6 | Scan and connectivity demonstrated; SDIO `ACTIVE_HIGH` reset-polarity fix confirmed across repeated cycles | AP, captive portal, reconnect, protocol workflows |
| Wi-Fi promiscuous/raw frame | Stock hosted interface lacks parity | Study custom C6 RPC/firmware, then capture/injection tests (legal RF environment) |
| BLE via C6 | Discovery (scan), advertising/GATT reported working | Every BLE command, error recovery and cross-radio coexistence |
| Local SD / files | Card works locally | Concurrent handles during `sdusb` transitions |
| Physical keyboard (A164) | Detected; basic typing | Input arbitration, key combinations, debounce/repeat |
| GPIO/DIO | P4 max GPIO handling plus two-tier reserved-pin protection (confirmable `PROTECTED_PINS` + no-escape `HARD_RESERVED_PINS` for the live C6 SDIO link) | Exhaustive available-pin vs reserved-pin table check |
| SPI / I2C / UART | Core ported | External loopback / analyzer-level protocol tests and rates |
| External RF & IR protocols | Adapters present in code | Radio-specific send/receive and timing traces |
| Flashrom serprog | Adapter present | `flashrom` client handshake/read/verify on disposable target |
| AVRDUDE / Bus Pirate | Adapter present | Real `avrdude` detect/verify against known device |
| OpenOCD Bus Pirate | Adapter present | Real OpenOCD session and target identification |
| BPIO2 / SUMP | Adapters present | Host-client handshake, capture length, sample timing |
| One-shot adapter launch | USB-UART entry re-tested | Each adapter boot path, watchdog behavior and exit |
| Release workflow / web flasher / M5Burner | GitHub Actions packaging infrastructure (`build-tab5-espidf.yml`, `release-tab5-espidf.yml`) | Clean build + physical flashing on supported tools |

**Scope deliberately excluded for now:** thermal benchmarking and long-duration endurance. Preserve as not tested, not passed.

## Community feasibility findings

The P4 silicon has dedicated HS OTG capability and independent USB Serial/JTAG. Tab5 power switching and the C6 companion introduce distinct integration responsibilities, but USB Device/HID/CDC/MSC functionality is feasible. Hosted BLE and ordinary Wi-Fi are demonstrably feasible. **Missing low-level Wi-Fi monitor/injection operations are not proof of a P4 silicon limitation**, but the C6/ESP-Hosted transport requires explicit investigation and likely custom firmware/RPC before equivalence can be claimed. Firmware maintenance and keeping the shared Bit Pirate core synchronized are as important to geo-tp's parity objective as protocol demonstrations.

- [Upstream contributor discussion / PR #176](https://github.com/geo-tp/ESP32-Bit-Pirate/pull/176)
- [ESP-Hosted project](https://github.com/espressif/esp-hosted-mcu)
- [ESP32-P4 USB documentation](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_overview.html)
