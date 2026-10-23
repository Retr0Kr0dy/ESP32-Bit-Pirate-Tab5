#!/usr/bin/env python3
"""Check for esp_hosted's early constructor, a known Tab5 boot issue.

We do not silently patch arbitrary dependency versions or report a compiled
firmware as hardware-qualified. For a release, the constructor must be removed
by a reviewed, version-pinned fix, or upstream must have resolved the problem.
"""
import argparse
from pathlib import Path

def check(root: Path) -> int:
    path = root / 'managed_components/espressif__esp_hosted/host/port/esp/freertos/src/port_esp_hosted_host_init.c'
    if not path.exists():
        print('WARN: Managed esp_hosted source not found. Cannot inspect early init.')
        return 2
    data = path.read_text(encoding='utf-8', errors='replace')
    has_ctor = '__attribute__((constructor))' in data or '__attribute__ ((constructor))' in data
    if has_ctor:
        print('UNSAFE FOR HARDWARE RELEASE: ESP-Hosted early constructor still present: ' + str(path))
        print('The original port required a local lazy-init patch, not supplied in its source ZIP.')
        return 1
    print('No plain constructor token found in ESP-Hosted init source; inspect lazy startup before flashing.')
    return 0

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--strict', action='store_true', help='Fail on missing or unsafe hosted patch')
    args = ap.parse_args()
    result = check(Path(__file__).resolve().parents[1])
    if args.strict and result:
        raise SystemExit(result)
