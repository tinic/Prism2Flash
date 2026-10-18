# Native emulator smoke results — 2026-10-18

## Experimental NV capture and corrected PDA CRC — 0.3.0-alpha.1

All **56 native guest checks passed** at 4096-byte Shell stack: Genesis 15/15
and Firmware 13/13 on each of Amiberry A600/68000/Kickstart 2.05 and Copperline
A1200/68020/Kickstart 3.1. Genesis adds bad-UTC rejection and the new backup
command's native directory/manifest/empty-slot failure path. Firmware uses
corrected CRC-16/ARC fixtures. The native helper independently checks SHA-256
of abc. These checks do not simulate the real card's NV data or timing.

[Exact outputs and hashes](evidence/emulator-smoke-0.3.0-alpha.1.json) retain
which binaries ran. Real A1200 acquisition outcomes are recorded separately
in HARDWARE-TESTING.md; emulator success does not certify physical backups.

## Mandatory-backup gate 0.2.1-alpha.1

All **52 native guest checks passed**, using a 4096-byte Shell stack.

| Utility | Emulator | Machine / CPU | Kickstart | Passed |
| --- | --- | --- | --- | --- |
| Prism2Genesis | Amiberry 8.4.1 | A600 / 68000 | 2.05 | 13/13 |
| Prism2Genesis | Copperline 1.0.0 | A1200 / 68020 | 3.1 | 13/13 |
| Prism2Firmware | Amiberry 8.4.1 | A600 / 68000 | 2.05 | 13/13 |
| Prism2Firmware | Copperline 1.0.0 | A1200 / 68020 | 3.1 | 13/13 |

Native `inspect` and both `probe` widths returned the mandatory full-backup
refusal before slot access. `info` retained its clean empty-slot exit; help,
version and argument handling also passed. The offline suite passed with
original synthetic images and saved card/PDA fixtures. Full-flash acquisition
is not implemented or tested. No hardware was accessed; no FS-UAE was used.

Binary sizes and SHA-256 values:

- `Prism2Genesis`: 53,952 bytes, `e1ff2f5fc52e48b0db1be59a7a3d20a57fefc337ec881f44db83860d929ff9bf`.
- `Prism2Firmware`: 69,176 bytes, `e960023f1945715aa01bff0f7ee3fc3e78c02d87d230f0a8e3d07df2efb57fe6`.

[Detailed evidence](evidence/emulator-smoke-0.2.1-alpha.1.json) retains all
outputs and return codes for these exact binaries. Earlier results below
predate the mandatory-backup policy.

## Offline parser milestone 0.2.0-alpha.1

All **78 native guest checks passed**, each using a 4096-byte Shell stack.

| Utility | Emulator | Machine / CPU | Kickstart | Passed |
| --- | --- | --- | --- | --- |
| Prism2Firmware | Amiberry 8.4.1 | A600 / 68000 | 2.05 | 13/13 |
| Prism2Firmware | Amiberry 8.4.1 | A1200 / 68020 | 3.1 | 13/13 |
| Prism2Firmware | Copperline 1.0.0 | A600 / 68000 | 2.05 | 13/13 |
| Prism2Firmware | Copperline 1.0.0 | A1200 / 68020 | 3.1 | 13/13 |
| Prism2Genesis | Amiberry 8.4.1 | A600 / 68000 | 2.05 | 13/13 |
| Prism2Genesis | Copperline 1.0.0 | A1200 / 68020 | 3.1 | 13/13 |

The offline suite checks help/version, flash- and RAM-image inspection,
individual and PRI/STA-pair validation against synthetic saved card/PDA data,
wrong-mode rejection, bad checksums, missing files/arguments, unsupported flash
commands and help after errors. It uses original synthetic payloads and
calibration; no proprietary firmware or actual card is used in a guest.
Genesis retains its startup/argument/empty-slot regression cases.

Binary sizes and SHA-256 values:

- `Prism2Genesis`: 59,028 bytes, `196365eada7c96937dce1795868a354003accc561f8347bf043c129c32643e13`.
- `Prism2Firmware`: 69,176 bytes, `834eb11e5a3e9706b6e558bbc7a922e3408f788bf9201fba02f1b34cee65ac53`.

[Detailed evidence](evidence/emulator-smoke-0.2.0-alpha.1.json) records every
output and exit code against these exact binary hashes. Use
`tests/emulator_smoke.py --suite firmware` for the offline suite and
`--suite genesis` for the existing probe suite. ROM/emulator options below
remain the same. No FS-UAE or physical hardware was used.

## Release candidate 0.1.0-alpha.1

All **52 checks passed** for the versioned native release candidate, using a
4096-byte Shell stack. Each profile ran the original twelve cases plus
`--version`, whose output matches VERSION exactly.

| Emulator | Machine / CPU | Kickstart | Result |
| --- | --- | --- | --- |
| Amiberry 8.4.1 | A600 / 68000 | 2.05 (37.350) | 13/13 passed |
| Amiberry 8.4.1 | A1200 / 68020 | 3.1 (40.68) | 13/13 passed |
| Copperline 1.0.0 | A600 / 68000 | 2.05 (37.350) | 13/13 passed |
| Copperline 1.0.0 | A1200 / 68020 | 3.1 (40.68) | 13/13 passed |

The release candidate is 59020 bytes. SHA-256:

```text
87742c58f1f640babbfdc4bc549410049177fc37e3914c1bcaceda35616ee1b2
```

[Release candidate evidence](evidence/emulator-smoke-0.1.0-alpha.1.json)
includes output and return codes for all 52 cases, ROM hashes and run identifiers.
This remains empty-slot startup testing; no physical Amiga or card was used.

## Original startup baseline

The actual Amiga executable passed all 48 command checks. Every invocation
used a 4096-byte Shell stack, with no Prism card attached.

| Emulator | Machine / CPU | Kickstart | Result |
| --- | --- | --- | --- |
| Amiberry 8.4.1 | A600 / 68000 | 2.05 (37.350) | 12/12 passed |
| Amiberry 8.4.1 | A1200 / 68020 | 3.1 (40.68) | 12/12 passed |
| Copperline 1.0.0 | A600 / 68000 | 2.05 (37.350) | 12/12 passed |
| Copperline 1.0.0 | A1200 / 68020 | 3.1 (40.68) | 12/12 passed |

## What ran

Each profile checked help, no arguments, invalid SRAM width, missing SRAM
width, rejection of the unimplemented flash command, `info`, `inspect`, both
probe widths, repeated `info`/`inspect`, and help after errors. Help returned
0; argument failures and empty-slot commands returned 20 as expected.

The active commands reached the native backend and reported:

```text
Prism2Genesis 0.1 - NATIVE AMIGA - hardware untested
Cannot own PCMCIA card: absent or held by a driver
```

Amiberry ran all commands in one boot, followed by a successful DOS environment
helper and a unique completion marker. Copperline used its direct `--run`
interface with `--exit-on-return`, launching a fresh guest for each case and
checking the emulator's propagated guest exit code. Both paths checked output
as well as return codes. The runner supports Amiberry and Copperline only.

## Startup fix

The initial binary failed before `main()` on a bare system because Newlib's
general formatting routines brought in `mathieeedoubbas.library`. The program
only formats integers and strings, so native formatting now uses `iprintf`,
`fiprintf` and `sniprintf`. A post-build check rejects references to the double
math libraries. The guard also rejected the saved original failing binary.

The tested executable is 57,536 bytes, built with m68k-amigaos GCC 16.2.0 for
68000. SHA-256:

```text
9a2a0759a92028102948bbe0e955c7b820f1c73c5ee832183850282da7240960
```

The [recorded evidence](evidence/emulator-smoke-2026-10-18.json) retains the
per-case results, ROM hashes, binary hash and unique run identifiers. The runner
writes detailed logs under the ignored `build-emulator/` directory; compact
result evidence is retained after build cleanup. ROMs are
external local assets and are not included in the repository.

## Reproduce

Build the native targets as described in the README, then run from the project
root with existing emulator and matching ROM files:

```sh
python3 tests/emulator_smoke.py --emulator copperline \
  --emulator-path /path/to/copperline --model A600 --cpu 68000 \
  --rom /path/to/a600-kickstart.rom
python3 tests/emulator_smoke.py --emulator amiberry \
  --emulator-path /path/to/Amiberry --model A1200 --cpu 68020 \
  --rom /path/to/a1200-kickstart.rom
```

Repeat with the other machine/CPU pair for the complete matrix. Each invocation
creates a private drive or launch directory and records `result.json`. The
runner stops only emulator processes that it started.

## Limits

These are startup and failure-path checks. No card registers, AUX transfers,
Genesis entry/exit, SRAM readback, firmware execution or flash programming
were exercised. No physical Amiga was accessed. Real-card validation remains
deferred under [HARDWARE-TESTING.md](HARDWARE-TESTING.md).
