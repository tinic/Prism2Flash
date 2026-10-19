# Card and firmware selection

1. Use a 16-bit Prism2 PC Card in the A600/A1200 PCMCIA slot.
2. Select primary and station images for its NIC platform using the
   [Intersil platform table](https://junsun.net/linux/intersil-prism/IDtable.html).
3. Pass the files to `Prism2Flash program`, or use `--firmware-dir Firmware`
   to select compatible files automatically. The utility reads this card's
   PDA and validates PRI/STA platform, interface, calibration and address records.

Firmware platform matching follows DOS ILHOPFW: the NIC ID word must occur in
an image platform record; revision fields do not restrict that match. RF/MAC/PRI
interface ranges and the full live-card/PDA identity are checked separately.

## Card detection

All manufacturer/product IDs in prism2.device 2.9's `product_codes` table and
Host AP's documented product-string matches are accepted. These include Netgear,
Linksys, D-Link, SMC, Compaq, Buffalo, Pretec, Canon, U.S. Robotics, Allied Telesyn,
Addtron, ZoomAir and Intersil reference cards. Detection is shared by backup,
Genesis and programming. Branding and NIC revision are not qualification gates.

The complete ID/string tables are in [compatibility.h](../include/compatibility.h).
Sources: [prism2.device 2.9](https://aminet.net/package/driver/net/prism2v2.lha),
[Host AP PC Card table](https://github.com/torvalds/linux/blob/v4.4/drivers/net/wireless/hostap/hostap_cs.c).

## Normal updates

The resident primary firmware handles flash programming using the DOS DOWNLOAD
commands. Its FD01 download buffer and FD00 load-time records determine the RAM
staging address, chunk size and timeout. No Genesis initial code or host JEDEC
algorithm is required. Station-only updates validate against the running
primary's FD03 interface range and preserve the installed primary bytes.

## Genesis and host flash commands

PDA PDR `0400` bit 1 selects x16 or x8 SRAM, following Host AP `prism2_srec`.
When absent, the loader uses Host AP's RAM readback detection. A RAM-size PDR
is retained. Both byte-write lanes and all initial-code bytes are verified.

The original DOS ILHOPFW host-programming paths are implemented:

| Live JEDEC ID | Flash | Unlock addresses |
|---|---|---|
| `01:6E` | AMD Am29LV010B | `0555` / `02AA` |
| `20:23` | ST M29W010B | `0555` / `02AA` |
| `BF:D5` | SST39VF010 | `5555` / `2AAA` |
| `1F:04`, `1F:05` | Atmel AT49BV/LV001(N)(T) | `5555` / `2AAA` |

The utility saves a full before backup, preserves CIS/PDA, programs the prepared
image, verifies every byte and saves a full after readback. Firmware files are
supplied by the user. The MA401 hardware run used initial `ID010001.HEX`, primary
`pk010101.hex` and station `sf010704.hex`.

## Firmware download recipes

The linked common images contain these NIC IDs in their platform records:

- `s1010701.hex`: 8002, 8003, 8004, 8008; station only, retaining the installed primary.
- `pf010100.hex`: 800A, 800B, 8012, 8016, 801A, 8021; primary.
- `pk010101.hex`: 800C, 8013, 8017, 801B, 8022; primary.
- `sf010704.hex`: 800A–800D, 8012–8014, 8016–8018, 801A–801C, 8021–8023; station.

8023 therefore uses station-only `sf010704.hex` with its running primary.
For 800E/8015/8019/801D, the four common downloads provide no flash image;
matching firmware must be supplied separately. M-family RAM images have a
nonzero S7 entry and are not interchangeable with flash images.

Model names span revisions. The quick start uses a specific firmware pair for
the tested MA401 V2.5 and the documented WPC11 v3/BroMax versions. Other models
and revisions use automatic selection. NIC 8004 is admitted from the S1010701
platform records, alongside the IDs in the release-ID table. The [Intersil ID table](https://junsun.net/linux/intersil-prism/IDtable.html)
provides the platform-to-memory mapping; the files' own platform records decide selection.
