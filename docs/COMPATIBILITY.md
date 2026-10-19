# Card and firmware selection

1. Use a 16-bit Prism2 PC Card in the A600/A1200 PCMCIA slot.
2. Select primary and station images for its NIC platform using the
   [Intersil platform table](https://junsun.net/linux/intersil-prism/IDtable.html).
3. Pass the files to `Prism2Flash program`, or use `--firmware-dir Firmware`
   to select compatible files automatically. The utility reads this card's
   PDA and validates PRI/STA platform, interface, calibration and address records.

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
