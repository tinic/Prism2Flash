# Installing the experimental Amiga utility

Download the `Prism2Flash-<version>-amiga-68000.lha` asset from
[GitHub Releases](https://github.com/tinic/Prism2Flash/releases). Obtain the
matching source archive and `SHA256SUMS` from the same release when retaining
or redistributing the binary. On a modern host, verify downloads with
`sha256sum --check SHA256SUMS` (or `shasum -a 256 --check SHA256SUMS` on macOS).
Download all listed assets before checking the complete manifest.

Extract the LHA into a drawer. The archive uses standard stored LHA entries;
no new decompressor format is needed. It contains:

- `C/Prism2Genesis`: native 68000 Genesis probe.
- `C/Prism2Firmware`: native offline image inspector/validator; see OFFLINE-FIRMWARE.md.
- `Libs/pccard.library`: unmodified pccard.library 2.2.
- Documentation, GPL/LGPL notices and the original complete pccard source
  archive in `third_party/pccard/pccard.lha`.
- Binary checksums, compiler/source metadata and the corresponding source URL.

Use an A600 or A1200 with AmigaOS 2.05 or later. The target has no FPU
requirement. This minimum configuration has emulator startup coverage;
hardware behavior is still unvalidated.

To inspect the version and help without accessing a card:

```text
Stack 8192
C/Prism2Genesis --version
C/Prism2Genesis --help
Version FILE C/Prism2Genesis
```

Emulator smoke tests use a 4096-byte Shell stack. Allow 8192 bytes for initial
hardware sessions to leave additional headroom for OS/library calls that have
not been exercised with a real card.

`info` uses card.resource. `inspect` and `probe` also require utility.library
and pccard.library 2.2 or later. Install the included pccard.library into
`LIBS:` only if no suitable version is already installed. Keep any newer
library. The library source and notices accompany the executable.

Before any active card test, read [HARDWARE-TESTING.md](HARDWARE-TESTING.md).
The tool must own the slot exclusively; stop the card's network driver first.
A probe resets the card and changes four bytes in its RAM. It does not program
flash, but real-card entry/exit and recovery behavior still need testing.
SRAM width refers to the card's internal memory and must come from the exact
hardware revision's documentation.

No Intersil firmware, Kickstart ROM or DOS utility is included. The normal
firmware flasher and dead-card Genesis bootstrap are future milestones.

The offline utility needs no pccard.library or network driver. Start with
`C/Prism2Firmware --help`; validation uses existing saved files and performs no
card operation. Both executables have independent `$VER` identifiers.
