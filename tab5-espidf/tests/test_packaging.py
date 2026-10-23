"""Host tests for the CI packaging code; require no ESP-IDF toolchain."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_firmware', ROOT / 'tools/package_firmware.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class FlashPackageTest(unittest.TestCase):
    def test_uses_real_offsets_and_preserves_all_parts(self):
        with tempfile.TemporaryDirectory() as d:
            r = Path(d)
            build = r / 'build'
            build.mkdir()
            (build / 'bootloader').mkdir()
            (build / 'partition_table').mkdir()
            for filename in ('bootloader/bootloader.bin', 'partition_table/partition-table.bin', 'app.bin', 'tab5-bitpirate-full.bin'):
                p = build / filename
                p.write_bytes(filename.encode())
            (build / 'flasher_args.json').write_text(json.dumps({
                'flash_files': { '0x2000': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'app.bin'},
                'extra_esptool_args': {'chip':'esp32p4'}}))
            out = mod.package(build, r / 'dist')
            self.assertTrue((r/'dist/tab5-bitpirate-firmware.zip').exists())
            self.assertIn('0x2000 parts/00002000-bootloader.bin', (r/'dist/FLASHING.txt').read_text())
            self.assertEqual(len(out), 8)
    def test_rejects_wrong_chip(self):
        with tempfile.TemporaryDirectory() as d:
            build = Path(d) / 'build'
            build.mkdir()
            (build / 'flasher_args.json').write_text(json.dumps({'extra_esptool_args':{'chip':'esp32s3'}, 'flash_files': {'0x1000':'a.bin'}}))
            with self.assertRaises(ValueError):
                mod.package(build, Path(d)/'dist')

if __name__ == '__main__':
    unittest.main()
