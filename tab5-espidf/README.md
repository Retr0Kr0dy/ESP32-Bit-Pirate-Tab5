# ESP32 Bit Pirate — M5Stack Tab5 (independent ESP-IDF port)

This is an **independent, alternative Tab5 port**, built from scratch on plain
ESP-IDF instead of extending the project's official PlatformIO/Arduino
toolchain. The community's current, official Tab5 support lives on the
`p4-tab5` branch and is built around M5Unified's own display/input stack, the
same way every other Bit Pirate board is. This port exists because the Tab5's
5″ capacitive touchscreen and ESP32-P4 can support a full graphical interface
and a native USB device role that the official branch's architecture doesn't
target — at the cost of being a second, separately-built firmware rather than
one more board in the shared PlatformIO environment.

It keeps the original Bit Pirate command grammar, protocol logic and
`ActionDispatcher` from `lib/BitPirateCore/` essentially unmodified (the core
command/controller/service layer is upstream-identical; only the board layer
is new), so every mode documented in the
[project wiki](https://github.com/geo-tp/ESP32-Bit-Pirate/wiki) behaves the
same way here. What's actually new for Tab5:

- **LVGL 8.3 touchscreen GUI** (`main/boards/tab5_bitpirate/Tab5Gui.cpp`) on a
  custom SGFX graphics core and SIC board HAL (`lib/SGFX/`, `lib/SIC/`, forked
  from Tab5-Orion/Tab5-Launchpad) — not M5Unified's display stack.
- **Native TinyUSB composite device** (`main/usb_device.c`): HID keyboard /
  mouse / gamepad / system-control, CDC-ACM and MSC in one descriptor, wired
  directly against esp_tinyusb rather than Arduino's own USB classes.
- **Web UI with captive portal** (`main/boards/tab5_bitpirate/WebUi.cpp`):
  HTTP + WebSocket CLI, LittleFS file browser/upload, served over the C6's
  Wi-Fi AP.
- **On-device AI assistant** (`main/boards/tab5_bitpirate/Tab5AiAssistant.*`): a Gemini-backed `ai`
  command reachable from the GUI, translating natural language into Bit
  Pirate commands.
- **Touch and physical keyboard input side by side**: an on-screen keyboard
  sharing the same `IInput` queue as an optional I2C keyboard accessory.
- Wi-Fi/BLE over the ESP32-C6 co-processor via `esp_hosted`, with its
  known early-init boot-crash worked around (see Build, below) — and the
  Tab5-specific RMT-DMA buffer alignment ESP32-P4 requires that ESP32-S3
  never needed, applied to CC1101 SubGHz raw capture and the I2C sniffer.

This is a separate firmware from the official `p4-tab5` PlatformIO
environment and from the ESP32-S3 boards — it is not meant to be overlaid
onto either, and isn't (yet) proposed for upstream merge.

Build, flash, boot (splash through the `ActionDispatcher`), screen rendering,
auto-rotation and the on-screen keyboard footer have all been verified on real
Tab5 hardware. Touch keyboard input, A164 priority and Wi-Fi/C6 bring-up still
need dedicated hardware qualification before release.

## What is here

- `lib/BitPirateCore/`: original Bit Pirate command/controllers/services, with small P4 adaptations.
- `main/boards/tab5_bitpirate/`: Tab5 terminal, LVGL touch interface, USB CDC bridge, web interface, AI command and input handling.
- `main/usb_device.c`: native ESP32-P4 USB device personalities (CDC/HID/MSC).
- `lib/SIC/` and `lib/SGFX/`: vendored native board and graphics support.
- `components/`: vendored protocol and Arduino-compatibility libraries.
- `sdkconfig.defaults`, `sdkconfig`, `dependencies.lock`, `main/idf_component.yml`: pinned board configuration and managed components.
- `tools/vendor_overrides/`, `tools/apply_vendor_overrides.py`: known-good replacements for managed-component files that don't build or boot correctly as fetched (see the Build section below).
- `main/boards/tab5_bitpirate/fonts/`: a real monospace font generated from DejaVu Sans Mono for the terminal canvas (LVGL's own bitmap fonts are proportional or don't render the ASCII banner correctly); `gen_font.py` regenerates it.
- `screenshot` command (`main/boards/tab5_bitpirate/Tab5Screenshot.cpp`): dumps the current screen as a base64 BMP over the serial/JTAG console, for debugging GUI rendering without physical access to the device.
- `.github/workflows/`: firmware build and manual pre-release publication.

## Build (ESP-IDF 5.5.4)

```sh
# In an ESP-IDF v5.5.4 environment; see Espressif's installation guide
python3 tools/check_source_tree.py
idf.py build
idf.py merge-bin -o tab5-bitpirate-full.bin
python3 tools/check_hosted_init.py
python3 tools/package_firmware.py --build-dir build --out-dir dist
```

The bundled `sdkconfig` is deliberately tracked as the last known Tab5 configuration; use `idf.py reconfigure` if changing dependencies. Its derived/generated output and `managed_components/` should never be committed. Do not upgrade ESP-IDF, Arduino, ESP-Hosted, or TinyUSB incidentally while diagnosing this port.

**Vendor component fixes are applied automatically.** Several managed components don't build or boot correctly as fetched on ESP32-P4/Tab5 (notably esp_hosted's eager early-init constructor, which boot-loops the Tab5 before `app_main` if left as shipped). `tools/apply_vendor_overrides.py` re-applies the known-good versions every configure, via a hook in the top-level `CMakeLists.txt` — no manual step needed. `tools/check_hosted_init.py` is a CI safety net that verifies the fix actually took effect before a release is published.

## Small borrowings from the official `p4-tab5` branch

A few quality-of-life details were ported over from the official PlatformIO
branch rather than reinvented: centralized P4 native-radio capability checks
(network operations through the remote C6 remain accessible, but monitor/
injection commands report the limitation), correct physical ESC/TAB/BACKSPACE
mapping, and bounded arrow-key hold-repeat. A `Tab5UsbCdcService` also exists
(backed by the vendored `components/iot_usbh_cdc`, Apache-2.0), meant to
bridge the Expander over the USB-A host port the way the official branch
does -- but it's wired in and disabled: real hardware testing found
`usb_host_install()`'s pump task starves `IDLE0`/CPU0 forever (P4 has two
USB-OTG controllers but one UTMI PHY, already claimed by the native
device-mode stack; this code never targets the other controller). The
Expander command falls back to the generic wired-UART transport instead.
Everything else described above — the LVGL GUI, native USB composite, Web UI,
AI assistant — is specific to this port and was not imported from there.

## GitHub Actions

- **Tab5 ESP-IDF firmware**: checks source layout and firmware packaging tests, builds the ESP32-P4 firmware in Espressif's ESP-IDF 5.5.4 environment, generates a full flash image with `idf.py merge-bin` and uploads the complete flash bundle as an Actions artifact.
- **Publish Tab5 firmware (manual)**: starts only from Actions with an existing version tag. It rebuilds the tagged commit, **enforces the ESP-Hosted constructor safety check**, and creates a GitHub pre-release with separate and merged flash files. No release is triggered automatically by a push.

### Using the Actions artifact

After a passing build, go to **Actions → Tab5 ESP-IDF firmware → latest run → Artifacts**. The package includes `tab5-bitpirate-full.bin`, `tab5-bitpirate-firmware.zip`, `parts/`, a machine-readable flash manifest, offsets and SHA-256 checksums. Flash instructions are generated from IDF's `flasher_args.json`, **not guessed**.

## Development policy

Keep Tab5 hardware adaptations under `main/boards/tab5_bitpirate/` wherever possible. Preserve original Bit Pirate command behavior in `lib/BitPirateCore`. Avoid build artifacts, one-off debug modes and generated or patched `managed_components/` in Git. For contributing features to upstream, use the separate upstream-style `p4-tab5` integration branch, not a wholesale transplant of this ESP-IDF project.

This code is derived from [ESP32-Bit-Pirate](https://github.com/geo-tp/ESP32-Bit-Pirate), with its MIT license retained in `LICENSE`. Vendored third-party code retains its own license files.
