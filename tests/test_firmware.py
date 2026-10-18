# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the real offline CLI against original synthetic and malformed data."""
import os
import hashlib
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
from firmware_fixtures import CARD, firmware, info, pda, srec, words, write_fixtures

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('P2_FIRMWARE_BIN', ROOT / 'build-host/Prism2Firmware')).resolve()


class FirmwareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='firmware-', dir=BINARY.parent)
        self.directory = Path(self.temp.name)
        write_fixtures(self.directory)

    def tearDown(self):
        self.temp.cleanup()

    def run_cli(self, *args, expected=0, contains=None):
        result = subprocess.run([str(BINARY), *args], cwd=self.directory, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout)
        if contains:
            self.assertIn(contains, result.stdout)
        return result.stdout

    def image(self, data):
        (self.directory / 'input.hex').write_bytes(data.encode() if isinstance(data, str) else data)
        return 'input.hex'

    def validate(self, image='station.hex', mode='flash', expected=0, contains=None, primary=None):
        args = ['validate', image, '--mode', mode, '--card', 'card.txt', '--pda', 'pda.bin']
        if primary:
            args += ['--primary', primary]
        return self.run_cli(*args, expected=expected, contains=contains)

    def reject(self, text, contains=None):
        return self.run_cli('inspect', self.image(text), expected=20, contains=contains)

    def test_inspect_metadata_and_separate_entry(self):
        text = self.run_cli('inspect', 'ram.hex')
        for value in ['id=0x001f major=1 minor=7 variant=4', 'Class: RAM; entry=0x003f0c01',
                      'address=0x007e1800 bytes=64', '80 bytes in 2 segments',
                      'id=0x8013 major=1 minor=0 variant=1', 'Name: SYNTHETIC.HEX',
                      'Build sequence: 123', 'Signature descriptor:', 'not computed CRC']:
            self.assertIn(value, text)

    def test_dos_initial_bootstrap_is_separate_from_firmware(self):
        text = (srec(3, 0x840, bytes(range(16))) + '\n' +
                srec(3, 0x7e0038, b'\x00\xe1\xa1\xff') + '\n' +
                srec(3, 0x7fe000, b'\x10\x32\x54\x76') + '\n' +
                srec(7, 0, b'') + '\n')
        name = self.image(text)
        report = self.run_cli('inspect-initial', name)
        for item in ['Initial RAM bootstrap: 24 bytes, 3 S3 records',
                     'zero S7 marker', 'address=0x00000840 bytes=16',
                     hashlib.sha256(text.encode()).hexdigest(),
                     hashlib.sha256(b'\x00\xe1\xa1\xff').hexdigest()]:
            self.assertIn(item, report)
        self.run_cli('inspect', name, expected=20, contains='Missing S7, component')
        self.run_cli('inspect-initial', 'station.hex', expected=20)
        self.validate(name, expected=20)
        for invalid in [
                text.replace(srec(7, 0, b''), srec(7, 0x7e0038, b'')),
                text.replace(srec(7, 0, b''), ''),
                srec(3, 0x841, b'\x00\x00') + '\n' + srec(7, 0, b''),
                srec(3, 0x840, b'\x00') + '\n' + srec(7, 0, b''),
                srec(3, 0x800000, b'\x00\x00') + '\n' + srec(7, 0, b''),
                srec(3, 0xff000000, bytes(12)) + '\n' + srec(7, 0, b''),
                srec(3, 0x840, bytes(16)) + '\n' + text,
                text[:-4] + '00\n']:
            self.run_cli('inspect-initial', self.image(invalid), expected=20)

    def test_valid_modes_and_pair(self):
        self.validate(contains='Offline metadata and PDA checks passed')
        self.validate('ram.hex', 'ram')
        self.validate(primary='primary.hex')
        self.run_cli('--help')
        self.run_cli('--version', contains=(ROOT / 'VERSION').read_text().strip())

    def test_validation_does_not_change_inputs(self):
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        self.validate(primary='primary.hex')
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})

    def test_reject_wrong_mode_and_implicit_arguments(self):
        self.validate(mode='ram', expected=20, contains='class differs')
        self.validate('ram.hex', 'flash', expected=20, contains='class differs')
        self.run_cli('validate', 'station.hex', expected=20)
        self.run_cli('flash', 'station.hex', expected=20)
        self.run_cli('inspect', 'missing.hex', expected=20, contains='Cannot open')
        self.run_cli('inspect', 'station.hex', '--force', expected=20)
        self.run_cli('validate', 'station.hex', '--mode', 'flash', '--mode', 'flash', expected=20)

    def test_checksum_length_hex_and_record_types(self):
        text = firmware()
        lines = text.splitlines(keepends=True)
        self.reject(lines[0][:-3] + '00\n' + ''.join(lines[1:]), 'checksum')
        self.reject(text.replace('S325', 'S324', 1), 'length')
        self.reject(text.replace('S325', 'S3ZZ', 1), 'Non-hexadecimal')
        self.reject('S113000000000000000000000000000000000000EC\n' + text, 'Unsupported')
        self.reject(text.encode() + b'\0', 'Malformed')
        self.reject('garbage\n' + text, 'Malformed')

    def test_termination_and_count(self):
        text = firmware()
        no_end = ''.join(text.splitlines(keepends=True)[:-1])
        self.reject(no_end, 'Missing S7')
        self.reject(text + srec(7, 0), 'Duplicate S7')
        # Real Intersil RAM images put the entry record before their payload.
        head, rest = no_end.split('\n', 1)
        self.run_cli('inspect', self.image(head + '\n' + srec(7, 0x3f0c01) + rest))
        count = sum(line.startswith('S3') for line in no_end.splitlines())
        self.run_cli('inspect', self.image(no_end + srec(5, count) + srec(7, 0)))
        self.reject(no_end + srec(5, count + 1) + srec(7, 0), 'record count')
        self.reject(no_end + srec(5, count) + srec(3, 0x2000, b'12') + srec(7, 0), 'after record count')

    def test_fragmented_metadata_crlf_no_final_newline_and_max_record(self):
        text = firmware(fragmented=True)
        self.run_cli('inspect', self.image(text.replace('\n', '\r\n').rstrip('\r\n')))
        extra = srec(3, 0x1000, bytes(range(250)))
        self.run_cli('inspect', self.image(firmware(extra=extra).replace('\n', '\r\n')))
        self.reject('S3' + 'AA' * 260 + '\n', 'too long')

    def test_metadata_malformed_duplicate_unknown_and_incomplete(self):
        self.reject(firmware(extra=info(1, words(0x1f, 4, 1, 7))), 'duplicate component')
        self.reject(firmware(extra=info(9, words(1))), 'Unknown firmware information')
        self.reject(firmware(extra=info(4, words(1))), 'platform identity')
        self.reject(firmware(extra=srec(3, 0xff200000, words(5, 1) + b'\0\0')), 'Unfinished')
        self.reject(firmware(extra=srec(3, 0xff200001, b'\0\0')), 'Discontinuous')
        self.reject(firmware(extra=srec(3, 0xff200000, words(0xffff, 1))), 'exceeds limit')
        self.reject(firmware(extra=srec(3, 0xff300000, b'12')), 'Unknown special')
        self.reject(firmware(extra=info(2, words(1, 3, 1, 5, 4))), 'Reversed')

    def test_payload_overlap_overflow_and_input_limits(self):
        self.reject(firmware(extra=srec(3, 0x7e1802, b'12')), 'Overlapping')
        self.reject(firmware(extra=srec(3, 0xffffff, b'12')), 'overflowing')
        self.reject(b'\n' * 65537, 'line limit')
        oversized = ''.join(srec(3, 0x10000 + i * 2, b'12') for i in range(8193))
        self.reject(oversized, 'segment limit')
        oversized = ''.join(srec(3, i * 250, bytes(250)) for i in range(4195))
        self.reject(oversized, 'Payload or segment limit')

    def test_exact_platform_and_pda_identity(self):
        self.validate(self.image(firmware(nic_variant=2)), expected=20, contains='Exact NIC')
        (self.directory / 'pda.bin').write_bytes(pda(nic=0x8014))
        self.validate(expected=20, contains='PDA NIC identity differs')
        (self.directory / 'pda.bin').write_bytes(pda(mac=b'\x02\x00\x00\x00\x00\x02'))
        self.validate(expected=20, contains='PDA MAC address differs')

    def test_pda_crc_length_duplicate_and_terminator(self):
        good = pda()
        for bad in [good[:-1], good + b'\0', bytes([good[0] ^ 1]) + good[1:]]:
            with self.subTest(length=len(bad)):
                (self.directory / 'pda.bin').write_bytes(bad)
                self.validate(expected=20)
        (self.directory / 'pda.bin').write_bytes(pda(duplicate=True))
        self.validate(expected=20, contains='Duplicate PDR')
        (self.directory / 'pda.bin').write_bytes(pda(missing=8))
        self.validate(expected=20, contains='NIC identity PDR')
        (self.directory / 'pda.bin').write_bytes(pda(missing=6))
        self.validate(expected=20, contains='RF/MAC supplier')

    def test_missing_or_wrong_length_plugs(self):
        (self.directory / 'pda.bin').write_bytes(pda(missing=3))
        self.validate(expected=20, contains='Required plug PDR')
        (self.directory / 'pda.bin').write_bytes(pda())
        extra = srec(3, 0xff000000, struct.pack('<III', 0x101, 0x7e1818, 8))
        self.validate(self.image(firmware(extra=extra)), expected=20, contains='plug length')

    def test_plug_and_signature_bounds(self):
        for extra, message in [
            (srec(3, 0xff000000, struct.pack('<III', 3, 0x7e2000, 4)), 'outside image'),
            (srec(3, 0xff000000, struct.pack('<III', 3, 0x7e1806, 4)), 'Overlapping PDA'),
            (srec(3, 0xff100000, struct.pack('<III', 0x7e1802, 8, 1)), 'does not begin'),
            (srec(3, 0xff100000, struct.pack('<III', 0x7e1800, 0x200, 1)), 'endpoints'),
        ]:
            with self.subTest(message=message):
                self.validate(self.image(firmware(extra=extra)), expected=20, contains=message)
        self.reject(firmware(extra=srec(3, 0xff000000, struct.pack('<III', 3, 0xffffff, 4))), 'plug address')
        self.reject(firmware(extra=srec(3, 0xff100000, struct.pack('<III', 0, 4, 1))), 'signature descriptor')

    def test_interfaces_pair_classes_and_overlap(self):
        self.validate(self.image(firmware(actor_variant=3)), expected=20, contains='variant or range')
        (self.directory / 'card.txt').write_text('P2CARD 1\nNIC 0x8013 1 1 0\nMAC 02:00:00:00:00:01\n')
        self.validate(expected=20, contains='supplier interface')
        self.validate(primary='primary.hex')
        self.validate(primary='ram.hex', expected=20, contains='PRI plus STA')
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, entry=0x3f0000))
        self.validate(primary='primary.hex', expected=20, contains='class differs')
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, address=0x7e1800))
        self.validate(primary='primary.hex', expected=20, contains='payload ranges overlap')

    def test_transport_bounds_and_component_scope(self):
        self.validate(self.image(firmware(address=0x800000)), expected=20, contains='AUX transport')
        self.validate(self.image(firmware(address=0x7e1801)), expected=20, contains='even-aligned')
        self.validate(self.image(firmware(component=0x14b)), expected=20, contains='Only PRI and STA')

    def test_snapshot_strict_parsing_and_pda_supplier_binding(self):
        (self.directory / 'card.txt').write_text(CARD.replace('SUP 3 2 3 6', 'SUP 3 08 03 06'))
        self.validate(self.image(firmware(actor_variant=8)))
        for bad in [CARD + 'NIC 0x8013 1 1 0\n', CARD + 'SUP 3 2 3 6\n',
                    CARD.replace('0x8013', '-1'), CARD.replace('0x8013', '65536'),
                    CARD.replace('0x8013', '0x8013junk'), CARD.replace('P2CARD 1', 'P2CARD 2'),
                    'P2CARD 1\nNIC 0x8013 1 1 0\0hidden']:
            with self.subTest(snapshot=bad):
                (self.directory / 'card.txt').write_bytes(bad.encode())
                self.validate(expected=20, contains='Card snapshot rejected')
        (self.directory / 'card.txt').write_text(CARD + 'SUP 1 2 1 3\n')
        self.validate(expected=20, contains='differs from PDA')

    def test_deterministic_malformed_input_stress(self):
        rng = random.Random(0x3842)
        valid = bytearray(firmware(fragmented=True).encode())
        for _ in range(120):
            bad = bytearray(valid)
            for _ in range(rng.randint(1, 8)):
                bad[rng.randrange(len(bad))] = rng.randrange(256)
            result = subprocess.run([str(BINARY), 'inspect', self.image(bad)], cwd=self.directory,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
            self.assertIn(result.returncode, (0, 20), result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
