#!/usr/bin/env python3
"""Copy tools/vendor_overrides/<path> onto managed_components/<path>, guarded
by tools/vendor_overrides_manifest.json's expected-original-hash check.

Run automatically from the top-level CMakeLists.txt right after project()
(with COMMAND_ERROR_IS_FATAL ANY, so a mismatch here fails the configure
step), once the IDF component manager has fetched managed_components/ for
this configure pass. Needed because several managed components don't build
or boot correctly as shipped on ESP32-P4/Tab5 (esp_hosted's eager early-init
constructor crash-loops boot; Tab5's SDIO pins aren't in any upstream
Arduino variant; Arduino's own USBCDC/USBMSC classes conflict with this
project's native TinyUSB/esp_tinyusb implementations; tinyusb's HID class
hardcodes rhport 0; LVGL's direct_mode sync uses the wrong area). See each
overridden file's own PATCHED comment for the specific reason. (The
ssl_client_stub link-ordering fix is handled separately, in the top-level
CMakeLists.txt, since it changes component registration itself -- too
structural for a plain file overwrite at this point in the configure.)

Three passes, deliberately not interleaved (this is the part that changed
from the original single-pass version -- see B/HARDWARE_TEST_TRACKER.md's
T17 and the corrective assessment's vendor-override atomicity finding for
why): validate every manifest entry first, copy only once every single one
has validated clean, then re-hash every copy to confirm it landed exactly as
expected. A single-pass copy-as-you-go loop can fail validation on entry 4
of 7 after already having copied 1-3, leaving the tree in a half-patched
state that's neither pristine-upstream nor fully overridden and that a
later configure pass's "already applied" check can't distinguish from
correct. Any failure here aborts before touching the filesystem at all.

Hash guard: before copying, each target's current content must match ONE of:
  - the override's OWN content (already applied this exact patch -- no-op,
    not re-copied, not an error)
  - the manifest's recorded PRISTINE-upstream hash for that path (expected,
    unpatched fetch -- apply the override)
Anything else -- the fetched file is neither the known-good upstream
original nor our own already-applied patch -- means the component manager
fetched something this patch was never verified against (most likely a
dependencies.lock version bump). That is treated as a hard error, not a
silent overwrite: the whole point is that an upstream change should be
*reviewed*, not transparently patched over and masked.

A manifest entry whose managed_components/ target does not exist at all is
likewise a hard error, not a silent skip: this script runs after the
component manager has already fetched managed_components/ for this
configure pass, so a missing target means the component itself failed to
fetch or was renamed/restructured upstream -- either way something is
wrong that the eventual build failure would prove anyway, just later and
with a far more confusing error.

To re-capture the manifest after a deliberate version bump in
dependencies.lock (espressif__tinyusb, lvgl__lvgl, espressif__arduino-esp32
or espressif__esp_hosted):
  1. Comment out this script's call in CMakeLists.txt.
  2. rm -rf managed_components && idf.py reconfigure (fetches pristine,
     unpatched files).
  3. sha256sum each tools/vendor_overrides/<path> against
     managed_components/<path>, write the new hashes into
     vendor_overrides_manifest.json.
  4. Restore the CMakeLists.txt call and reconfigure again to confirm the
     override now applies cleanly.
"""
import hashlib
import json
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OVERRIDES = ROOT / "tools" / "vendor_overrides"
TARGET = ROOT / "managed_components"
MANIFEST_PATH = ROOT / "tools" / "vendor_overrides_manifest.json"


def sha256_of(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    if not OVERRIDES.is_dir():
        return 0

    manifest = {}
    if MANIFEST_PATH.is_file():
        with MANIFEST_PATH.open() as f:
            manifest = json.load(f)
    manifest.pop("_comment", None)

    sources = [src for src in sorted(OVERRIDES.rglob("*")) if src.is_file()]

    # Pass 1: validate every single override before copying any of them.
    to_copy = []  # list of (src, dst, src_hash)
    errors = []
    for src in sources:
        rel = src.relative_to(OVERRIDES)
        rel_str = rel.as_posix()
        dst = TARGET / rel

        if not dst.is_file():
            errors.append(
                f"'{rel_str}': managed_components/ target does not exist "
                f"(expected at '{dst.relative_to(ROOT)}'). The component "
                f"manager should have fetched this before this script runs "
                f"-- check it actually fetched, wasn't renamed upstream, or "
                f"wasn't restructured by a dependencies.lock version bump."
            )
            continue

        src_hash = sha256_of(src)
        dst_hash = sha256_of(dst)

        if dst_hash == src_hash:
            continue  # already patched; nothing to do for this one

        expected_original = manifest.get(rel_str)
        if expected_original is None:
            errors.append(
                f"'{rel_str}': no manifest entry in {MANIFEST_PATH.name} -- "
                f"cannot verify this override is safe to apply. Add its "
                f"expected pristine-upstream SHA-256 to the manifest (see "
                f"this script's header for how) before it can be patched."
            )
            continue

        if dst_hash != expected_original:
            errors.append(
                f"'{rel_str}': fetched content matches neither the "
                f"already-applied patch nor the manifest's expected "
                f"pristine-upstream hash.\n"
                f"    fetched:  {dst_hash}\n"
                f"    expected: {expected_original}\n"
                f"    This usually means dependencies.lock moved this "
                f"component to a different version than this override was "
                f"verified against. Review the new upstream file, update "
                f"the override under tools/vendor_overrides/ if still "
                f"needed, and re-capture the manifest entry (see this "
                f"script's header) -- do not just delete the manifest "
                f"entry to make this pass."
            )
            continue

        to_copy.append((src, dst, src_hash))

    if errors:
        print(
            f"apply_vendor_overrides: FATAL: {len(errors)} override(s) "
            f"failed validation; nothing was copied.",
            file=sys.stderr,
        )
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    # Pass 2: every override validated clean -- now copy all of them.
    for src, dst, _ in to_copy:
        shutil.copyfile(src, dst)

    # Pass 3: re-hash every copy just made and confirm it landed intact.
    verify_errors = []
    for src, dst, src_hash in to_copy:
        actual = sha256_of(dst)
        if actual != src_hash:
            verify_errors.append(
                f"'{dst.relative_to(ROOT)}': post-copy hash {actual} does "
                f"not match the source override's {src_hash} -- copy did "
                f"not land as expected (disk error, concurrent write?)."
            )

    if verify_errors:
        print(
            f"apply_vendor_overrides: FATAL: {len(verify_errors)} file(s) "
            f"failed post-copy verification.",
            file=sys.stderr,
        )
        for e in verify_errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    skipped = len(sources) - len(to_copy)
    if to_copy:
        print(f"apply_vendor_overrides: applied {len(to_copy)} file(s), verified")
    if skipped:
        print(
            f"apply_vendor_overrides: {skipped} file(s) already patched, skipped"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
