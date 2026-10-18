# SPDX-License-Identifier: GPL-2.0-only
"""Byte-exact preparation checks; legacy code sees only our valid synthetic data."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest

from firmware_fixtures import CARD, firmware, pda, srec, write_fixtures

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('P2_FIRMWARE_BIN', ROOT / 'build-host/Prism2Firmware')).resolve()
DUMP = Path(os.environ.get('P2_PREPARE_BIN', ROOT / 'build-host/prepare_dump')).resolve()


def legacy_oracle(directory):
    with tarfile.open(ROOT / 'third_party/hostap/hostap-utils-0.4.7.tar.gz') as archive:
        source = archive.extractfile('hostap-utils-0.4.7/prism2_srec.c').read().decode()
        util = archive.extractfile('hostap-utils-0.4.7/util.h').read().decode()
    # Unchanged upstream structures/parser/plug/stamp/merge routines. Exclude
    # Linux device code. The unreachable device-specific 0400 plug aborts.
    types = util[util.index('#define PRISM2_PDA_SIZE'):util.index('int read_wlan_pda(')]
    code = '#include <errno.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
    code += types + source[source.index('static int verbose = 0;'):source.index('enum { ROLE_SUPPLIER')]
    code += source[source.index('unsigned char *get_s3_data_pos'):source.index('static int supported_platform')]
    code += 'static int plug_pdr_0400(const char *name, unsigned char *p) { abort(); }\n'
    code += source[source.index('int plug_pdr_entries'):source.index('/* Verify compatibility of the image(s)')]
    code += r'''
int main(int argc, char **argv) {
    struct wlan_info wlan = {0};
    struct prism2_pdr records[256];
    unsigned char bytes[1024];
    struct srec_data *a, *b = NULL;
    size_t pos = 0, i, j;
    FILE *f;
    assert(argc == 3 || argc == 4);
    f = fopen(argv[2], "rb"); assert(f);
    assert(fread(bytes,1,sizeof(bytes),f) == sizeof(bytes)); fclose(f);
    wlan.pda.pdrs = records;
    while (pos + 4 <= sizeof(bytes)) {
        unsigned words = bytes[pos] | (bytes[pos+1] << 8);
        unsigned id = bytes[pos+2] | (bytes[pos+3] << 8);
        if (!id) break;
        assert(words && pos + (words+1)*2 <= sizeof(bytes));
        assert(wlan.pda.pdr_count < 256);
        records[wlan.pda.pdr_count++] = (struct prism2_pdr){id,(words-1)*2,bytes+pos+4};
        pos += (words+1)*2;
    }
    a = read_srec(argc == 4 ? argv[3] : argv[1]); assert(a);
    assert(!plug_pdr_entries(&wlan,a)); assert(!generate_crc16(a));
    if (argc == 4) {
        b = read_srec(argv[1]); assert(b);
        assert(!plug_pdr_entries(&wlan,b)); assert(!generate_crc16(b));
        assert(!merge_s3_data(a,b));
    }
    puts("CANONICAL"); printf("I %u\n",a->start_addr);
    for (i=0; i<(size_t)a->s3_used; ++i) {
        printf("S %u %u ", a->s3_records[i].addr,a->s3_records[i].len);
        for (j=0;j<(size_t)a->s3_records[i].len;++j) printf("%02x",a->s3_records[i].data[j]);
        puts("");
    }
    free_srec(a); if(b) free_srec(b); return 0;
}
'''
    path = directory / 'legacy.c'
    path.write_text(code)
    binary = directory / 'legacy'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu99', '-w', str(path), '-o', str(binary)], check=True)
    return binary


class PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='prepare-', dir=BINARY.parent)
        cls.directory = Path(cls.temp.name)
        cls.legacy = legacy_oracle(cls.directory)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        write_fixtures(self.directory)

    def prepare(self, target='station.hex', mode='flash', primary=None, error=None, dos=False):
        args = [str(BINARY), 'prepare', target, '--mode', mode, '--card', 'card.txt', '--pda', 'pda.bin']
        if primary:
            args += ['--primary', primary]
        if dos:
            args += ['--plug-policy', 'dos']
        before = {p.name: p.read_bytes() for p in self.directory.iterdir() if p.is_file()}
        result = subprocess.run(args, cwd=self.directory, capture_output=True, text=True, timeout=15)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 20 if error else 0, output)
        self.assertIn(error or 'Offline preparation passed', output)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir() if p.is_file()})
        return output

    def compare(self, target='station.hex', primary=None):
        inputs = [target, 'pda.bin']
        current = [str(DUMP), *inputs, 'card.txt'] + ([primary] if primary else [])
        # Upstream rejects S0 headers; it receives the same payload/metadata
        # without our supported synthetic header. Preserve the actual basename.
        reference = self.directory / 'reference'
        reference.mkdir(exist_ok=True)
        for name in [target] + ([primary] if primary else []):
            raw = (self.directory / name).read_bytes()
            (reference / name).write_bytes(b''.join(line for line in raw.splitlines(keepends=True)
                                                   if not line.startswith(b'S0')))
        legacy = [str(self.legacy), str(reference / target), 'pda.bin']
        if primary:
            legacy += [str(reference / primary)]
        outputs = [subprocess.check_output(cmd, cwd=self.directory, timeout=15).split(b'CANONICAL\n')[1]
                   for cmd in (current, legacy)]
        self.assertEqual(*outputs)
        return outputs[0]

    def test_upstream_station_primary_ram_and_pair(self):
        self.compare()
        self.compare(primary='primary.hex')
        self.compare('ram.hex')
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, entry=0x123401))
        self.compare('primary.hex')
        self.compare('ram.hex', 'primary.hex')
        self.prepare('ram.hex', 'ram', 'primary.hex')

    def test_exact_patches_stamps_and_source_hashes(self):
        output = self.prepare()
        expected = bytearray(range(64))
        expected[4:10] = b'\x02\x00\x00\x00\x00\x01'
        expected[16:20] = b'TEST'
        expected[32:48] = b'station.hex'.ljust(16, b'\0')
        prepared = b'\xde\xc0' + expected
        self.assertIn('address=0x007e17fe bytes=66 sha256=' + hashlib.sha256(prepared).hexdigest(), output)
        canonical = self.compare()
        self.assertIn(prepared.hex().encode(), canonical)
        for ending in ('\n', '\r\n', ''):
            data = firmware().replace('\n', '\r\n') if ending == '\r\n' else firmware().rstrip('\n') + ending
            (self.directory / 'station.hex').write_bytes(data.encode())
            output = self.prepare()
            self.assertIn('Source SHA-256: ' + hashlib.sha256(data.encode()).hexdigest(), output)
        self.assertIn('PDA SHA-256: ' + hashlib.sha256(pda()).hexdigest(), output)

    def test_filename_truncation_and_empty_metadata_name(self):
        name = 'a-very-long-source-filename.hex'
        (self.directory / name).write_text(firmware(include_name=False))
        output = self.compare(name)
        self.assertIn((name[:15].encode() + b'\0').hex().encode(), output)
        self.prepare(name)

    def test_upstream_signature_boundary_merge(self):
        # The 64-byte primary ends exactly at the station payload start.
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, address=0x7e17c0))
        output = self.compare(primary='primary.hex')
        self.assertIn(b'S 8263616 128 ', output)
        self.prepare(primary='primary.hex')

    def test_standalone_primary_signature_placeholder(self):
        placeholder = srec(3, 0x7e17fe, b'\x00\x00')
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, extra=placeholder))
        self.compare(primary='primary.hex')
        self.prepare(primary='primary.hex')

    def test_reject_signature_overlap_and_calibration_loss(self):
        signature = srec(3, 0xff100000, struct.pack('<III', 0x7e1800, 0x110, 1))
        (self.directory / 'station.hex').write_text(firmware(extra=signature))
        self.prepare(error='Duplicate signature')
        (self.directory / 'station.hex').write_text(firmware(extra=srec(3, 0x7e17fe, b'AB')))
        self.prepare(error='does not begin')
        write_fixtures(self.directory)
        plug = srec(3, 0xff000000, struct.pack('<III', 3, 0x7e17fc, 4))
        (self.directory / 'primary.hex').write_text(firmware(component=0x15, address=0x7e17c0, extra=plug))
        self.prepare(primary='primary.hex', error='overwrite a PRI PDA plug')

    def test_reject_primary_flash_bad_pda_and_pair(self):
        self.prepare('primary.hex', error='Primary-only flash')
        self.prepare(mode='ram', error='class differs')
        (self.directory / 'pda.bin').write_bytes(pda(missing=3))
        self.prepare(error='Required plug PDR')
        (self.directory / 'pda.bin').write_bytes(pda(mac=b'\x02\x00\x00\x00\x00\x02'))
        self.prepare(error='MAC address differs')

    def test_dos_optional_defaults_are_whole_fields_and_reported(self):
        (self.directory / 'pda.bin').write_bytes(pda(missing=3))
        output = self.prepare(dos=True)
        expected = bytearray(range(64))
        expected[4:10] = b'\x02\x00\x00\x00\x00\x01'
        expected[32:48] = b'STATION.HEX'.ljust(16, b'\0')
        # Missing field remains precisely the bytes supplied by the HEX.
        self.assertIn('address=0x007e17fe bytes=66 sha256=' +
                      hashlib.sha256(b'\xde\xc0' + expected).hexdigest(), output)
        self.assertIn('PDR=0x0003 expected=4 actual=0 missing', output)
        (self.directory / 'pda.bin').write_bytes(pda())
        old = srec(3, 0xff000000, struct.pack('<III', 3, 0x7e1810, 4))
        new = srec(3, 0xff000000, struct.pack('<III', 3, 0x7e1810, 6))
        (self.directory / 'station.hex').write_text(firmware().replace(old, new))
        output = self.prepare(dos=True)
        self.assertIn('PDR=0x0003 expected=6 actual=4 length mismatch', output)
        self.assertIn(hashlib.sha256(b'\xde\xc0' + expected).hexdigest(), output)

    def test_dos_defaults_do_not_replace_missing_rf_calibration(self):
        plug = srec(3, 0xff000000, struct.pack('<III', 0x202, 0x7e1818, 2))
        (self.directory / 'station.hex').write_text(firmware(extra=plug))
        self.prepare(dos=True, error='Required plug PDR')


if __name__ == '__main__':
    unittest.main()
