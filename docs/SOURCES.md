# Sources and findings

Research and source inspection: 2026-10-18. Limited A1200 acquisition checks
are recorded in HARDWARE-TESTING.md; complete hardware validation is pending.

- [Host AP utilities 0.4.7](https://w1.fi/releases/hostap-utils-0.4.7.tar.gz):
  `prism2_srec.c`, `util.c`, `util.h`, `hostap_common.h`, README and COPYING.
  Original archive retained in third_party/hostap for the planned parser port.
  GPL-2.0-only, Jouni Malinen. The Linux socket/ioctl/procfs interface is not
  needed by a direct Amiga backend.
- Linux v4.4 Host AP:
  [hostap_download.c](https://github.com/torvalds/linux/blob/v4.4/drivers/net/wireless/hostap/hostap_download.c),
  [hostap_cs.c](https://github.com/torvalds/linux/blob/v4.4/drivers/net/wireless/hostap/hostap_cs.c),
  [hostap_hw.c](https://github.com/torvalds/linux/blob/v4.4/drivers/net/wireless/hostap/hostap_hw.c),
  [hostap_wlan.h](https://github.com/torvalds/linux/blob/v4.4/drivers/net/wireless/hostap/hostap_wlan.h).
  The Genesis/AUX sequence is adapted from these GPL sources; unmodified copies
  are retained in third_party/hostap/linux-v4.4. Upstream authors include
  SSH Communications Security Corp and Jouni Malinen.
- [prism2.device 2.9](https://aminet.net/package/driver/net/prism2v2.lha):
  Neil Cafferkey's `Source/pccard.c`, `unit.c`, `prism2.h`, `io.h`, and manual
  establish the existing Amiga mapping, byte order and AUX access. GPL-2.0-or-later.
- [pccard.library 2.2](https://aminet.net/package/util/libs/pccard): unmodified
  archive, runtime binary and developer header retained in third_party/pccard.
  Original per-file GPL/LGPL notices apply. Source archive includes the complete
  library source. Version 2.2 fixed misaligned access on 68000/68010.
- AmiNetXDuo `src/netdev/netdev_pcmcia.c`: MIT-licensed reference for
  card.resource call stubs, ownership and removal semantics. Its original MIT
  license is retained in third_party/AmiNetXDuo-LICENSE.
- [GetCardMap autodoc](https://d0.se/autodocs/card.resource/GetCardMap),
  [OwnCard autodoc](https://d0.se/autodocs/card.resource/OwnCard), and
  [CardResetCard autodoc](https://d0.se/autodocs/card.resource/CardResetCard).
- [Intersil platform release-ID map](https://junsun.net/linux/intersil-prism/IDtable.html):
  NICID, SRAM/flash configuration and firmware platform relationships.
- [Jun Sun flashing guide](https://junsun.net/linux/intersil-prism/): primary/
  station pairing, compatibility checks, filename conventions and recovery references.
- [Documented DOS recovery](https://github.com/patters-match/prism2-flashing#-recovering-from-a-bad-flash):
  a failed MA401RA flash recovered using ILHOPFW and a matching initial firmware.
  Its suggestion that Host AP disappeared after Linux 2.6.32 is contradicted by
  the v4.4 source inspected here. Its successful recovery does not validate our
  Amiga implementation or establish a general failure/recovery rate.

Genesis is chipset behavior, not a DOS-specific feature. The ordinary flash
path also uses card firmware commands rather than host-driven JEDEC programming.
Whether each path behaves correctly through Gayle remains an empirical question.

File checksums for retained third-party artifacts are in third_party/SHA256SUMS.

The compiler asset version, per-platform SHA-256 pins and the AmiNetXDuo
toolchain repair/check source commit are recorded in `tools/toolchain.json`.
The retained repair/check scripts are MIT-licensed; see
`third_party/AmiNetXDuo-LICENSE`. Project builds download compiler assets
separately and verify their hashes before extraction.

Flash's corrected 2026-10-18 firmware analysis and our independent parser
comparison are summarized in FIRMWARE-FINDINGS.md. Metadata/payload hashes are
retained under docs/evidence; proprietary firmware payloads are excluded.


## NV acquisition and PDA CRC correction

Host AP `prism2_enable_aux_port` zeroes PARAM0/1/2 for AUX disable and checks
CONTROL bits 15:14; it does not restore arbitrary prior scratch values.
The linux-wlan-ng `src/prism2/driver/hfa384x.c` reference (AbsoluteValue
Systems, GPL v2 or MPL 1.1) ends `hfa384x_copy_from_aux` by clearing AUXOFFSET
while AUX remains enabled. These sequences inform the experimental reader's
cleanup; pointer register readback is diagnostic, not a restorable setting.

The retained Host AP 0.4.7 `util.c` CRC table starts 0000,c0c1,c181,0140;
`parse_wlan_pda` checks a zero full-PDA residue. This is CRC-16/ARC reflected
polynomial 0xa001, initial zero. The former 0x8408 code and fixtures were wrong.
The independent fixed synthetic vector in test_nv_backup.c prevents a shared
parser/fixture error. Prior S-record metadata/payload comparisons did not
validate PDA CRC behavior.

DeepSeek's private offline analysis was independently repeated against the
raw 8 MiB MA401RA capture. Aliasing suggests a 128 KiB window; geometry and
complete firmware contents remain unverified. The majority-derived image is
separate evidence and never changes the original raw capture. Private data
and firmware images are not distributed with the project.
