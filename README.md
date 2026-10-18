# Prism2Flash

Native Amiga utilities for Intersil Prism II/2.5/3 PCMCIA cards, developed
alongside AmiNetXDuo. The project starts with Genesis recovery and will then
add a regular firmware flasher using the same programming engine.

**Status: first Genesis probe implemented, host checks passing, native 68000
binary built. No real card or Amiga has been tested.** The A1200 is currently
in use by another project; hardware testing is deferred.

## First executable: Prism2Genesis

This first milestone establishes PCMCIA ownership, AUX memory access and the
Genesis reset sequence. It implements:

- `info`: claim the slot and report the CIS manufacturer/product identifiers.
  This does not configure the card or issue Prism commands.
- `inspect`: configure the I/O window, initialize the card, read NIC/primary/
  station identities and validate a 1024-byte PDA read including its CRC.
- `probe --sram 8` or `probe --sram 16`: perform those checks, enter Genesis,
  run the Host AP four-byte SRAM readback test at `0x7e0038`, return to normal
  mode, and check that primary firmware responds again.

The probe resets the card and changes four bytes in its RAM. It requires
exclusive slot ownership and interrupts any use of that card for networking.
`--sram` means the card's internal SRAM width, **not** the Amiga's PCMCIA bus
width. Determine it from documentation for the exact hardware revision.
There is no automatic width guessing or force option.

The current program requires working primary firmware, readable CIS data and
a valid PDA. It does not yet load bootstrap firmware, revive a dead card, save
backups, or program flash. Those steps are explicit milestones in
[ROADMAP.md](ROADMAP.md). No command `0x22` programming path is compiled into
the executable.

## Native build

Requires CMake and a `m68k-amigaos-gcc` cross toolchain with Amiga NDK headers.
The target is 68000 with AmigaOS 2.05 or later. Hardware compatibility with
that minimum OS is still to be tested.

```sh
export AMIGA_TOOLCHAIN_ROOT=/path/to/m68k-amigaos-toolchain
cmake -S . -B build-amiga -DCMAKE_TOOLCHAIN_FILE=cmake/amiga.cmake
cmake --build build-amiga
```

The Amiga executable is `build-amiga/Prism2Genesis`.
`inspect` and `probe` require `utility.library` and **pccard.library 2.2 or
newer**. An unmodified pccard.library 2.2 is provided in
`third_party/pccard/Libs`, together with its complete original source archive
and license notices. Do not replace a newer installed library.

The initial configured-card allowlist is deliberately narrow: CIS identifiers
`0274:1612`, `0274:1613`, `0250:0002`, `028a:0002`, `02aa:0002`,
`02d2:0001`, followed by a second check of the running NICID. This list comes
from upstream code; it is **not a hardware-tested compatibility list**.
Ambiguous NICID `800c`, early HFA3841 cards, multifunction cards and Prism 3
SSF cards await dedicated profiles. A 3.3 V-only or CardBus card is not a
target for the A600/A1200 slot.

## Host verification

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
build-host/Prism2GenesisSim probe --sram 8
```

`Prism2GenesisSim` is a deterministic protocol model. It does not connect to
an Amiga, emulate Gayle or establish real-card compatibility. The tests check
both SRAM control values, AUX page boundaries and byte order, RID/PDA
validation, deadlines including clock wrap, incorrect SRAM selection,
readback errors, removal/cancellation, and rejection of flash commands.

See [docs/HARDWARE-TESTING.md](docs/HARDWARE-TESTING.md) for the deferred test
procedure, [docs/PROTOCOL.md](docs/PROTOCOL.md) for the implementation contract,
and [docs/SOURCES.md](docs/SOURCES.md) for provenance and research.

## License

Project source is GPL-2.0-only; see [COPYING](COPYING). Third-party code and
materials retain their own notices. No Intersil firmware image is bundled.
