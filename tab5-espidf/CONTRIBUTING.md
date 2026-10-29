# Contributing to the Tab5 port

This directory (`tab5-espidf/`) is a Tab5-only experimental firmware tree inside the fork; do not mix ESP32-S3 PlatformIO changes into it, and don't submit Tab5 ESP-IDF changes against the S3 source tree at the repo root. Cross-board Bit Pirate behavior must stay as close as possible to the original [geo-tp project](https://github.com/geo-tp/ESP32-Bit-Pirate). When changing command grammar, controller behavior, mode dispatch or adapters, note whether the change should also go to upstream.

For every contribution, include the tested P4 revision, ESP-IDF version, relevant C6 firmware version (if applicable), reproduction steps, expected/actual behavior and a scope statement: source-only, simulated, on-device positive path or full protocol test. Keep MAC addresses, SSIDs, eFuse dumps, private host paths and complete raw host USB inventories out of public issues and commits.

Run `python3 -m unittest discover -s tests -v` and `python3 tools/check_source_tree.py` locally, followed by a clean `idf.py build` when possible. Do not bypass the managed override hashes in `tools/vendor_overrides_manifest.json`. Add or update the corresponding row in `docs/FEATURE_PARITY.md` and keep unresolved capabilities visibly unresolved. No eFuse-burning recipes, unsafe USB VBUS backfeed experiments or unrestricted concurrent SD/MSC writes should be introduced as 'tests'.

Preserve the MIT license and original attribution. Imported dependencies retain their separate notices. A future PR to geo-tp should justify the extra LVGL/SIC/SGFX HAL rather than pretending there is no maintenance impact.
