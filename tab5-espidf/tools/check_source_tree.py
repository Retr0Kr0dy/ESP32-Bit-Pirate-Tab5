#!/usr/bin/env python3
"""Small zero-dependency structural check, suitable before IDF downloads."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
needed = [
    'CMakeLists.txt', 'main/CMakeLists.txt', 'main/idf_component.yml',
    'sdkconfig.defaults', 'dependencies.lock', 'partitions/partitions.csv',
    'main/boards/tab5_bitpirate/BitPirateTask.cpp',
    'lib/BitPirateCore/Dispatchers/ActionDispatcher.cpp',
    'main/usb_device.c', 'main/boards/tab5_bitpirate/Tab5Gui.cpp',
]
missing = [p for p in needed if not (root / p).is_file()]
if missing:
    raise SystemExit('Missing files: ' + ', '.join(missing))
cmake = (root / 'main/CMakeLists.txt').read_text(encoding='utf-8')
# Verify explicitly referenced vendored source files still exist.
refs = re.findall(r'\$\{(?:BPCORE_DIR|CMAKE_CURRENT_LIST_DIR)\}/([^\s)]+\.(?:cpp|c))', cmake)
missing_refs = []
for rel in refs:
    base = root / ('lib/BitPirateCore' if '${BPCORE_DIR}/' + rel in cmake else 'main')
    if not (base / rel).is_file():
        missing_refs.append(str(base.relative_to(root) / rel))
if missing_refs:
    raise SystemExit('CMake source(s) missing: ' + ', '.join(missing_refs))
assert 'CONFIG_IDF_TARGET="esp32p4"' in (root / 'sdkconfig.defaults').read_text()
assert 'ActionDispatcher' in (root / 'main/boards/tab5_bitpirate/BitPirateTask.cpp').read_text()
print(f'Source tree OK ({len(refs)} explicit core/main source references verified).')
