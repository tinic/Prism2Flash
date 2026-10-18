# Prism2Flash

## Flash your Prism2 card

Prism2 PC Cards documented by Host AP and prism2.device are recognized.
SRAM width and firmware compatibility are read from the card itself.
[Card and firmware selection](docs/COMPATIBILITY.md).

1. Extract the latest [Amiga release](https://github.com/tinic/Prism2Flash/releases).
   Install the included `Libs/pccard.library` in `LIBS:` if version 2.2 or newer is absent.
2. Download the initial, PRI and STA files matching your card.
   MA401 example: [initial](https://junsun.net/linux/intersil-prism/dos-resurrection/ID010001.HEX), [PRI 1.1.1](https://junsun.net/linux/intersil-prism/firmware/1.7.4/pk010101.hex), [STA 1.7.4](https://junsun.net/linux/intersil-prism/firmware/1.7.4/sf010704.hex).
3. Stop WirelessManager and disconnect the card from your network stack.
4. From the extracted drawer, run (substitute your three firmware filenames):

```text
Stack 16384
MakeDir Work:P2Backups
C/Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

Create the backup drawer once; keep **20 MiB free**. Timestamped subdrawers,
one full before backup, one full after readback and checksum checks are automatic.
No clock setup is required.

5. After **success**, power-cycle the card/Amiga and verify:

```text
C/Prism2Flash verify --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

**Recovery required:** keep power on; do not retry programming or reboot.
[Help and recovery details](docs/NATIVE-FLASH.md). No firmware is bundled.

[![CI](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml)
[![CodeQL](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/tinic/Prism2Flash?include_prereleases)](https://github.com/tinic/Prism2Flash/releases)

Native Amiga utilities for Intersil Prism II/2.5/3 PCMCIA cards, developed
alongside AmiNetXDuo. Project code uses C++23 and the AmigaPorts GCC 16.2-rc17
toolchain.

**Current hardware milestone:** the actual Netgear MA401 V2.5 / PD5LMWP200 has
passed x16 Genesis, original DOS initial-code loading, SST BF:D5 identification,
native programming and complete intended-image readback. The tested firmware pair is PRI 1.1.1 / STA 1.7.4.
See [hardware results](docs/HARDWARE-TESTING.md) for the exact runs, including
failures and recovery.

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

## Mandatory backups before active card operations

Every impactful operation must save a fresh, verified, timestamped **full flash**
backup first, followed by a timestamped full readback afterward. This includes
active inspection, resets and Genesis RAM probes, as well as loading,
flashing and restores. PDA/CIS-only backups are insufficient. Backup failure
blocks the operation; no skip/force option is permitted.

Native operations use the card's PDA, exclusive slot ownership and
fresh verified captures. There is no force/skip option. Old saved captures do
not authorize a new operation. See the
[backup contract](docs/HARDWARE-TESTING.md#mandatory-full-flash-backups).

## Native utilities

- `Prism2Genesis --help`: CIS information, full NV backup, preflight and the
  Prism2 x8/x16 Genesis/bootstrap transaction.
- `Prism2Firmware --help`: offline S-record inspection, compatibility checks
  and private-copy preparation. It has no card backend.
- `Prism2Flash --help`: native programming and post-boot verification with full
  before/after captures. Use `Stack 16384`. See the
  [complete command reference](docs/NATIVE-FLASH.md) for compatible inputs,
  storage, intended-image checks and recovery behavior.

```text
Stack 16384
Prism2Flash --help
Prism2Flash --version
```

The original DOS ILHOPFW.EXE supplies the Genesis double latch, initial-code
loading/handoff and SST host programming sequence. Amiga adaptations include
card.resource ownership, Gayle byte lanes, AUX selector cleanup and persistent
backup transactions; see [the derivation](docs/DOS-REFERENCE.md).

The full NV backup command reads all 8 MiB twice without INIT or reset:

```text
Prism2Genesis backup --output Work:Prism2Backups/MA401-TIMESTAMP --utc TIMESTAMP
```

Use current UTC in `YYYYMMDDTHHMMSSZ` form. Keep both complete raw files and
manifest. For impactful operations, use the owned transaction commands rather
than treating this standalone capture as authorization.

For read-only analysis of existing MA401 captures, use
`python3 tools/audit_nv_capture.py FIRST SECOND`. It checks the saved CIS/PDA,
aliases and firmware anchors without accessing hardware. Host protocol-model
commands remain available as `Prism2GenesisSim`; simulated results are separate
from real-card results.

## Offline tool: Prism2Firmware

`Prism2Firmware inspect IMAGE` decodes a Prism S-record image and reports its
component, RAM/flash class, platforms, interface ranges, PDA plugs and signature
descriptors. `validate` checks it against a saved NIC/MAC/supplier snapshot and
a CRC-checked binary PDA, with optional replacement-primary compatibility.
`prepare` applies PDA plugs, source filename fields and C0DE stamps to private
copies, merges an optional PRI/STA pair and reports byte counts and SHA-256.
It reads files only and leaves source/PDA files unchanged. No card backend is linked.

See [offline commands and data formats](docs/OFFLINE-FIRMWARE.md) and
[Flash's incorporated findings](docs/FIRMWARE-FINDINGS.md). The parser was
compared byte-for-byte with Host AP on seven privately supplied image variants;
normal CI uses original synthetic fixtures and includes no proprietary firmware.
The native flasher builds and verifies its complete intended image from the fresh owned backup; the offline tool continues to report prepared payloads without card access.

## Native build

Requires CMake 3.24+ and a C++23 `m68k-amigaos-g++` cross toolchain with Amiga NDK headers.
The pinned native compiler is AmigaPorts v16.2-rc17 (GCC 16.2.1).
Project sources use LLVM formatting enforced by CI; see [contributing](CONTRIBUTING.md).
The target is 68000 with AmigaOS 2.05 or later. Hardware compatibility with
that minimum OS is still to be tested.

```sh
export AMIGA_TOOLCHAIN_ROOT=/path/to/m68k-amigaos-toolchain
cmake -S . -B build-amiga -DCMAKE_TOOLCHAIN_FILE=cmake/amiga.cmake
cmake --build build-amiga
```

The Amiga executables are `build-amiga/Prism2Genesis`,
`build-amiga/Prism2Firmware` and `build-amiga/Prism2Flash`. The host offline executable is `build-host/Prism2Firmware`.
For a pinned compiler with its startup/ABI checks applied automatically, run
`tools/ci.sh native` on Linux x86_64 or macOS arm64. This does not require an
AmiNetXDuo checkout. Host checks use `tools/ci.sh host`.
`backup`, `inspect` and `probe` require `utility.library` and **pccard.library 2.2 or
newer**. An unmodified pccard.library 2.2 is provided in
`third_party/pccard/Libs`, together with its complete original source archive
and license notices. Do not replace a newer installed library.

Card recognition uses the documented Host AP and prism2.device PC Card IDs
and product strings. The card's CRC-checked PDA supplies its NIC identity and
x8/x16 SRAM configuration. Firmware platform and interface compatibility are
checked against that identity. The DOS parallel-flash engine selects the
AMD, ST, SST or Atmel command sequence from the live JEDEC ID.
See [compatibility and firmware selection](docs/COMPATIBILITY.md).

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
and its limits. Actual card results are recorded separately in
[HARDWARE-TESTING.md](docs/HARDWARE-TESTING.md).

## License

Project source is GPL-2.0-only; see [COPYING](COPYING). Third-party code and
materials retain their own notices. No Intersil firmware image is bundled.

## Implementation

See [C++ design](docs/CPP-DESIGN.md) for ownership, typed results, bounded
storage, card interfaces and the Amiga ABI boundary.
