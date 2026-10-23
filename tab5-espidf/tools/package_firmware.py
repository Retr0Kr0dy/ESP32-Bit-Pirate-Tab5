#!/usr/bin/env python3
"""Repack an ESP-IDF build into a portable release, using actual flash offsets.

Do not guess image offsets or skip any generated flash partition.
Run after: idf.py build && idf.py merge-bin -o tab5-bitpirate-full.bin
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

def package(build: Path, dest: Path) -> list[Path]:
    desc_path = build / 'flasher_args.json'
    data = json.loads(desc_path.read_text(encoding='utf-8'))
    chip = data.get('extra_esptool_args', {}).get('chip', '').lower().replace('-', '')
    if chip != 'esp32p4':
        raise ValueError(f'Unexpected build chip: {chip!r} (expected esp32p4)')
    flash_files = data.get('flash_files', {})
    if not flash_files:
        raise ValueError('No files in flasher_args.json')
    items = sorted(((int(offset, 0), file) for offset, file in flash_files.items()), key=lambda x:x[0])
    dest.mkdir(parents=True, exist_ok=True)
    output: list[Path] = []
    parts = dest / 'parts'
    parts.mkdir(exist_ok=True)
    for offset, file in items:
        source = build / file
        if not source.is_file():
            raise FileNotFoundError(source)
        name = f'{offset:08x}-{source.name}'
        out = parts / name
        shutil.copy2(source, out)
        output.append(out)
    merged = build / 'tab5-bitpirate-full.bin'
    if not merged.is_file():
        raise FileNotFoundError('Missing merged firmware, run idf.py merge-bin first')
    output_image = dest / 'tab5-bitpirate-full.bin'
    shutil.copy2(merged, output_image)
    output.append(output_image)
    flash_lines = [f'0x{offset:x} parts/{offset:08x}-{Path(file).name}' for offset, file in items]
    note = dest / 'FLASHING.txt'
    note.write_text('ESP32 Bit Pirate / M5Stack Tab5 - ESP32-P4\n'
                    'Images are UNVERIFIED until tested on real hardware.\n'
                    'The esp_hosted constructor patch must be reviewed before release.\n\n'
                    'Merged image (offset 0x0):\n'
                    '  esptool --chip esp32p4 -p PORT write-flash 0x0 tab5-bitpirate-full.bin\n\n'
                    'Separate images (from flasher_args.json, do not change offsets):\n'
                    '  esptool --chip esp32p4 -p PORT write-flash ' + ' '.join(flash_lines) + '\n'
                    'For development: idf.py -p PORT flash monitor\n', encoding='utf-8')
    output.append(note)
    checksum = dest / 'SHA256SUMS.txt'
    checksum.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(dest).as_posix()}\n' for p in output), encoding='utf-8')
    output.append(checksum)
    # Keep the actual machine-readable offset table for downstream flashers.
    shutil.copy2(desc_path, dest / 'flasher_args.json')
    output.append(dest / 'flasher_args.json')
    archive = dest / 'tab5-bitpirate-firmware.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for p in output:
            zf.write(p, p.relative_to(dest).as_posix())
    print(f'Firmware package: {archive} ({len(items)} flash parts)')
    return output + [archive]

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--out-dir', type=Path, default=Path('dist'))
    args = parser.parse_args()
    package(args.build_dir, args.out_dir)
