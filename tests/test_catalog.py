# SPDX-License-Identifier: GPL-2.0-only
"""Select complete compatible pairs from mixed, renamed and malformed firmware."""
from pathlib import Path
import subprocess
import os
import sys
import tempfile
from firmware_fixtures import pda, firmware, info, words
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(dir=binary.parent, prefix='catalog-') as temp:
    folder = Path(temp)
    def run(files, expected, nic=0x800c, resident=False):
        (folder/'pda.bin').write_bytes(pda(nic=nic, variant=1))
        names=[]
        for name,text in files.items():
            (folder/name).write_text(text)
            names.append(name)
        result = subprocess.run([str(binary),'pda.bin',*names],cwd=folder,text=True,capture_output=True, env=dict(os.environ, **({"P2_CATALOG_RESIDENT":"1"} if resident else {})))
        if expected is None:
            assert result.returncode == 20, result
        else:
            assert result.returncode == 0 and result.stdout.strip() == expected, result
    for nic in (0x8000,0x8002,0x8003,0x8004,0x8008,0x8009,0x800a,0x800b,0x800c,0x800d,
                0x800e,0x8012,0x8013,0x8014,0x8015,0x8016,0x8017,0x8018,0x8019,
                0x801a,0x801b,0x801c,0x801d,0x8022,0x8023):
        run({'renamed-primary.hex':firmware(component=0x15,nic=nic,nic_variant=2),
             'renamed-station.hex':firmware(nic=nic,nic_variant=3)},'0 1',nic)
    pri=firmware(component=0x15,nic=0x800c)
    sta=firmware(nic=0x800c)
    newer=sta.replace(info(1,words(0x1f,4,1,7)),info(1,words(0x1f,2,1,8)))
    run({'not-firmware.hex':'broken','p.hex':pri,'s.hex':sta,'new.hex':newer,
         'wrong-platform.hex':firmware(nic=0x800b),
         'ram.hex':firmware(nic=0x800c,entry=0x3f0000)},'1 3')
    run({'p.hex':pri,'bad-interface.hex':firmware(nic=0x800c,actor_variant=9)},None)
    run({'p.hex':pri,'wrong-platform.hex':firmware(nic=0x800b)},None)
    run({'s.hex':sta},None)
    run({'s.hex':sta},'1 0',resident=True)
    run({'bad-interface.hex':firmware(nic=0x800c,actor_variant=9)},None,resident=True)
print('Automatic firmware selection: all PC Card platforms, metadata, pairs and faults passed')
