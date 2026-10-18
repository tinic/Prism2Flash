# Prism2Flash

[![CI](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml)
[![CodeQL](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/tinic/Prism2Flash?include_prereleases)](https://github.com/tinic/Prism2Flash/releases)

Native Amiga utilities for Intersil Prism II/2.5/3 PCMCIA cards, developed
alongside AmiNetXDuo. The project starts with Genesis recovery and will then
add a regular firmware flasher using the same programming engine.

**Status: Genesis probe plus offline firmware parser/validator implemented; host checks and native executable
smoke tests pass in Amiberry and Copperline. No real card or physical Amiga
has been tested.** The A1200 is currently in use by another project; hardware
testing is deferred.

## Downloads and project workflow

Get versioned Amiga binaries from [Releases](https://github.com/tinic/Prism2Flash/releases).
Each release includes an Amiga 68000 LHA, complete corresponding source,
checksums and compiler/source metadata. See [installation](docs/INSTALL.md),
[changelog](CHANGELOG.md), [contributing](CONTRIBUTING.md) and
[versioning/CI/release procedures](docs/RELEASING.md).

`VERSION` drives `Prism2Genesis --version` and the embedded Amiga `$VER`
identifier. Version tags run host tests, sanitizers, native compilation and
archive verification before publishing. Initial releases are experimental
prereleases while real-card validation is pending.

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

## Offline tool: Prism2Firmware

`Prism2Firmware inspect IMAGE` decodes a Prism S-record image and reports its
component, RAM/flash class, platforms, interface ranges, PDA plugs and signature
descriptors. `validate` checks it against a saved NIC/MAC/supplier snapshot and
a CRC-checked binary PDA, with optional replacement-primary compatibility.
It reads files only and leaves the firmware unchanged. No card backend is linked.

See [offline commands and data formats](docs/OFFLINE-FIRMWARE.md) and
[Flash's incorporated findings](docs/FIRMWARE-FINDINGS.md). The parser was
compared byte-for-byte with Host AP on seven privately supplied image variants;
normal CI uses original synthetic fixtures and includes no proprietary firmware.
Preparation, bootstrap loading and actual flashing remain later milestones.

## Native build

Requires CMake 3.24+ and a `m68k-amigaos-gcc` cross toolchain with Amiga NDK headers.
The target is 68000 with AmigaOS 2.05 or later. Hardware compatibility with
that minimum OS is still to be tested.

```sh
export AMIGA_TOOLCHAIN_ROOT=/path/to/m68k-amigaos-toolchain
cmake -S . -B build-amiga -DCMAKE_TOOLCHAIN_FILE=cmake/amiga.cmake
cmake --build build-amiga
```

The Amiga executables are `build-amiga/Prism2Genesis` and
`build-amiga/Prism2Firmware`. The host offline executable is `build-host/Prism2Firmware`.
For a pinned compiler with its startup/ABI checks applied automatically, run
`tools/ci.sh native` on Linux x86_64 or macOS arm64. This does not require an
AmiNetXDuo checkout. Host checks use `tools/ci.sh host`.
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

## Emulator smoke tests

Version 0.2.0-alpha.1 passed 78 native checks using a 4096-byte Shell stack:
52 offline file-processing checks on Amiberry/Copperline with A600/68000 and
A1200/68020 profiles, plus 26 Genesis startup/no-card regression checks on
Amiberry A600 and Copperline A1200. No physical card or Amiga was used.

The smoke test caught an unnecessary Newlib double-math library dependency
that prevented startup on a bare system. The native CLI now uses integer-only
formatting, and the build rejects that dependency if it reappears.

`tests/emulator_smoke.py` creates a fresh isolated drive and records exit
codes, output and binary/ROM hashes. It uses Copperline's direct executable
launch interface, or a bare boot drive in Amiberry. Supply your existing ROM
assets; none are copied into the repository. FS-UAE is not used by this runner.

```sh
python3 tests/emulator_smoke.py --suite firmware --emulator copperline \
  --emulator-path /path/to/copperline --model A600 --cpu 68000 \
  --rom /path/to/matching-a600-kickstart.rom
python3 tests/emulator_smoke.py --emulator amiberry \
  --emulator-path /path/to/Amiberry --model A1200 --cpu 68020 \
  --rom /path/to/matching-a1200-kickstart.rom
```

See [docs/EMULATOR-RESULTS.md](docs/EMULATOR-RESULTS.md) for the recorded result
and its limits. Physical card testing remains deferred.

## License

Project source is GPL-2.0-only; see [COPYING](COPYING). Third-party code and
materials retain their own notices. No Intersil firmware image is bundled.
