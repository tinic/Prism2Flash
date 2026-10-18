# Installation

Start with [the short flashing guide](QUICKSTART.md).

Download the `Prism2Flash-<version>-amiga-68000.lha` asset from
[GitHub Releases](https://github.com/tinic/Prism2Flash/releases). Obtain the
matching source archive and `SHA256SUMS` from the same release when retaining
or redistributing the binary. On a modern host, verify downloads with
`sha256sum --check SHA256SUMS` (or `shasum -a 256 --check SHA256SUMS` on macOS).
Download all listed assets before checking the complete manifest.

Extract the LHA into a drawer. The archive uses standard stored LHA entries;
no new decompressor format is needed. It contains:

- `C/Prism2Genesis`: native 68000 Genesis/bootstrap and full-backup utility.
- `C/Prism2Flash`: qualified MA401 SST programming and post-boot verification.
- `C/Prism2Firmware`: offline inspection, validation and preparation reports; see OFFLINE-FIRMWARE.md.
- `Libs/pccard.library`: unmodified pccard.library 2.2.
- Documentation, GPL/LGPL notices and the original complete pccard source
  archive in `third_party/pccard/pccard.lha`.
- Binary checksums, compiler/source metadata and the corresponding source URL.

Use an A600 or A1200 with AmigaOS 2.05 or later. The target has no FPU
requirement. This minimum configuration has emulator startup coverage;
hardware qualification is restricted to the MA401 profile documented in HARDWARE-TESTING.md.

To inspect the version and help without accessing a card:

```text
Stack 16384
C/Prism2Genesis --version
C/Prism2Genesis --help
Version FILE C/Prism2Genesis
```

Emulator startup checks include a 4096-byte Shell stack. Use 16384 bytes for
native hardware transactions, as used in the actual programming session.

`info` uses card.resource. `backup`, `inspect` and `probe` also require utility.library
and pccard.library 2.2 or later. Install the included pccard.library into
`LIBS:` only if no suitable version is already installed. Keep any newer
library. The library source and notices accompany the executable.

The native flasher is restricted to the exact MA401 V2.5 / PD5LMWP200 profile
with pinned initial code and validated compatible PRI/STA inputs. Read
[NATIVE-FLASH.md](NATIVE-FLASH.md) before hardware use. It describes the fresh
full captures, persistent storage, command syntax and recovery
hold. No firmware, Kickstart ROM or DOS utility is included. General dead-card
recovery and other card profiles are not enabled.

The offline utility needs no pccard.library or network driver. Start with
`C/Prism2Firmware --help`; validation uses existing saved files and performs no
card operation. All three executables have independent `$VER` identifiers.

## Experimental NV capture

```text
Stack 16384
C/Prism2Genesis backup --output Work:Prism2Backups/MA401RA-20261018T120000Z --utc 20261018T120000Z
```

Supply the actual current UTC timestamp from a trusted clock; the example is
only syntax. The parent drawer must already exist. The final drawer must be
new, contain the timestamp, and be on persistent storage (RAM: and T: are
refused). Reserve at least 17 MiB plus filesystem overhead. Keep both raw files
and the manifest, including failed or partial captures. The command targets the
exact MA401RA/ISL37300P CIS only and does not certify the physical flash map.
No active command is enabled by this capture. See HARDWARE-TESTING.md for the
actual hardware results and unresolved limitations.
