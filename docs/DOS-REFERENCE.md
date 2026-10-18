# Original DOS utility reference

The user requires original DOS ILHOPFW.EXE to be the reference for Genesis,
bootstrap and programming. Host AP and prism2.device corroborate the transport.

- Intersil PRISM Flash Utility **0.5e, Apr 24 2002**, Intel v1.0.7.
- EXE: 157300 bytes, SHA-256
  `f7e2ec28585dc17315983e7f10bb44874811fd58c751f818efc9f38bb8a9bbe5`.
- [Original mirror](https://junsun.net/linux/intersil-prism/dos-resurrection/):
  matching EXE/INI, `prismdos.txt`, `DOScd.txt`, `ID010001.HEX`.
- Complete paginated `ILHOPFW -?` executed in DOSBox 0.74-3-3 and captured.
- Original executable and firmware stay in private research evidence.
- DeepSeek Flash was explicitly assigned binary reverse engineering.

MZ load starts at file **0x2a00**. Hardware segment **0a22** has file base
**0xcc20**; card-I/O segment **1960** has file base **0x1c000**.

| Original location | Verified observation |
| --- | --- |
| Help `-3842 <GenNor>` | High byte selects Genesis straps; low byte normal straps. `0F07` selects `0x0f` / `0x07`. |
| Help `-i` / `-gen` | `-i` loads an initial image into RAM and sets Genesis; `-gen` selects the subsequent Genesis path. |
| Help `-on` | Keeps card power/configuration after exit, retaining RAM recovery state across DOS invocations. |
| Help `-hf` | Host flash algorithm instead of firmware algorithm. |
| `0a22:3803`, file `0x10423` | AUX seek to `0x7e0038`, write words `E100`, `FFA1`, seek/read/compare. AUX enable and one repeat on initial failure. |
| `0a22:0ad5` | Reset dispatcher uses COR `C5`/`45`, configured Genesis/normal HCR bytes, idle routine and 5 ms delays. Full branch interpretation is being independently traced. |
| `1960:1ba6` | PC Card COR byte writes at attribute offset `0x3e0`. |
| `1960:1c19` | PC Card HCR byte writes at `0x3e2`; PCI register is `0x5c`. |

Amiga configuration base and I/O window come from CIS. Explain any adaptation
of the DOS PCIC COR index and socket controls rather than copying them blindly
into Gayle.

## Initial image

The original `ID010001.HEX` is 10372 bytes; SHA-256
`1d326cee5887005e9b755e74e5578ccfbd74ae5e7ebc19c1af23e61b7a76c018`.
It has 218 S3 records, 3434 payload bytes in seven regions, no PRI/STA metadata
and a zero S7 marker. `Prism2Firmware inspect-initial` handles this separate raw
RAM dialect without weakening regular image validation. Zero S7 here is not
flash classification or a generic execution address. Loading still requires
the DOS-derived initial-image start sequence and exact card binding.
