# Native emulator smoke results — 2026-10-18

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
per-case results, ROM hashes, binary hash and unique run identifiers. Full
local logs remain under the ignored `build-emulator/` directory. ROMs are
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
