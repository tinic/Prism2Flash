# SPDX-License-Identifier: GPL-2.0-only
"""Archive compatibility checks for downloadable release artifacts."""
import datetime
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_release import crc16, write_lha
from check_project import metadata


class ReleaseTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(crc16(b'123456789'), 0xbb3d)

    def test_metadata(self):
        version, date = metadata()
        self.assertTrue(version)
        self.assertGreaterEqual(date.year, 2026)

    @unittest.skipUnless(shutil.which('lha'), 'Lhasa is needed for independent extraction')
    def test_lha_roundtrip_and_reproducibility(self):
        files = {'Prism2Flash/C/test': bytes(range(256)) * 31,
                 'Prism2Flash/docs/empty': b'',
                 'Prism2Flash/docs/readme': b'Native Amiga binary package\n'}
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            first, second = temp / 'first.lha', temp / 'second.lha'
            for path in (first, second):
                write_lha(path, files, datetime.date(2026, 10, 18))
            self.assertEqual(hashlib.sha256(first.read_bytes()).digest(),
                             hashlib.sha256(second.read_bytes()).digest())
            subprocess.run(['lha', 'tq', str(first)], check=True)
            subprocess.run(['lha', 'xfq', str(first)], cwd=temp, check=True)
            for name, expected in files.items():
                self.assertEqual((temp / name).read_bytes(), expected)


if __name__ == '__main__':
    unittest.main()
