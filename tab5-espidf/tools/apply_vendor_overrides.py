#!/usr/bin/env python3
"""Copy tools/vendor_overrides/<path> onto managed_components/<path>.

Run automatically from the top-level CMakeLists.txt right after project(),
once the IDF component manager has fetched managed_components/ for this
configure pass. Needed because several managed components don't build or
boot correctly as shipped on ESP32-P4/Tab5 (esp_hosted's eager early-init
constructor crash-loops boot; Tab5's SDIO pins aren't in any upstream
Arduino variant; Arduino's own USBCDC/USBMSC classes conflict with this
project's native TinyUSB/esp_tinyusb implementations; tinyusb's HID class
hardcodes rhport 0; LVGL's direct_mode sync uses the wrong area). See each
overridden file's own PATCHED comment for the specific reason. (The
ssl_client_stub link-ordering fix is handled separately, in the top-level
CMakeLists.txt, since it changes component registration itself -- too
structural for a plain file overwrite at this point in the configure.)

Silently does nothing for an override whose managed_components/ target
doesn't exist yet (not fetched this pass) -- the next configure that does
fetch it will apply it then.
"""
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OVERRIDES = ROOT / "tools" / "vendor_overrides"
TARGET = ROOT / "managed_components"


def main() -> int:
    if not OVERRIDES.is_dir():
        return 0
    applied = 0
    for src in OVERRIDES.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(OVERRIDES)
        dst = TARGET / rel
        if not dst.parent.is_dir():
            continue
        shutil.copyfile(src, dst)
        applied += 1
    if applied:
        print(f"apply_vendor_overrides: applied {applied} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
