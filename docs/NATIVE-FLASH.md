# Native Prism2 firmware update

**Start here: [quick start](QUICKSTART.md).**

`Prism2Flash` is a native Amiga command. `Prism2Flash --help` and
`Prism2Flash --version` do not access the card. Use `Stack 16384` for hardware
operations. Firmware files are not distributed with this project.

Card recognition follows the Host AP and prism2.device compatibility tables.
The fresh PDA supplies the NIC identity and x8/x16 SRAM configuration; firmware
metadata is checked against this card. The DOS host-flash engine selects AMD,
ST, SST or Atmel commands from its live JEDEC identification.
See [card and firmware selection](COMPATIBILITY.md).

## Simple commands

Put your firmware files in `Firmware`, then use:

```text
Prism2Flash program --firmware-dir Firmware --backup-dir Work:P2Backups
Prism2Flash verify --firmware-dir Firmware --backup-dir Work:P2Backups
```

The card's fresh PDA and image metadata select the compatible PRI/STA pair.
If no primary image matches, a compatible station image can use the installed
primary firmware. No initial image is needed for normal updates.
For multiple compatible pairs, the highest station version, then primary version,
is selected. Keep only the firmware versions you want in the drawer.

Use `--backup-dir EXISTING_DRAWER`. The tool creates a fresh subdrawer,
saves the before/after captures and checks their hashes automatically.
**No clock setup is required.** Dates are best-effort labels only. If the clock
is unreadable, the label is `untimed`; duplicate labels get a numeric suffix.
An unset, incorrect, frozen or changing clock cannot block flashing.

For a chosen output name, use `--output NEW_DRAWER` instead:

```text
Prism2Flash program --primary pk010101.hex --station sf010704.hex --output Work:P2Backups/Upgrade1
Prism2Flash verify --primary pk010101.hex --station sf010704.hex --output Work:P2Backups/Check1
```

The parent must exist and have 20 MiB free. Existing output drawers are refused
to preserve backups. Legacy `--utc`/`--utc-offset` arguments affect labels only;
they are optional and never validate the system clock.

`--expected-plan-sha` additionally pins the entire prepared 128 KiB image. The
image includes this card's own PDA, CIS and unchanged gaps, so its hash is
card-specific. The option is accepted by both commands; saved hashes never
authorize another insertion or replace fresh captures.

The hashes below identify the MA401 inputs used in the completed Genesis hardware
run. Normal updates need only PRI/STA, or STA alone. Compatible images pass the
metadata, platform, interface, PDA and address checks; hashes are saved automatically:

| Input | Component | SHA-256 |
|---|---|---|
| ID010001.HEX | Original DOS initial RAM code | `1d326cee5887005e9b755e74e5578ccfbd74ae5e7ebc19c1af23e61b7a76c018` |
| pk010101.hex | PRI 1.1.1 | `20638ee6180583488f769d95ed0b240b115bb8b8f6a72e2597dd72d58b4b7791` |
| sf010804.hex | STA 1.8.4 | `9b2161583291ebde062a70015288d2b3f3cd7dfdcd9eb9f05d2fd18c418a66bb` |
| sf010704.hex | STA 1.7.4 | `92e3101220003bed0caeebe42d0817e96db67b1541c62c7601e7772f1c6aa3e5` |

## What programming does

The utility claims the slot exclusively and takes one complete 8 MiB AUX NV
capture. It checks all 128 KiB aliases for exact equality, then closes, reopens
and hashes the saved file before INIT, Genesis or flash work. Preparation uses the PDA from that same fresh capture. It overlays
only validated image segments onto the complete old 128 KiB image, preserves
the CIS/PDA interval and every other gap, saves the intended image, reopens it
and checks its hash.

Normal updates follow the DOS resident-firmware DOWNLOAD path. The running
primary supplies its interface range (FD03), download buffer (FD01) and maximum
load time (FD00). The utility reserves each chunk with command `0222`, stages
and verifies its bytes in RAM, commits with `0322` and checks NV readback. It
programs the complete preserved image, verifies it again and ends DOWNLOAD with
`0022`, waits the DOS 100 ms settling interval, then closes AUX only if this
operation enabled it. Pre-enabled AUX is left alone; it is never forced back on
after restart. The after-capture owns its own AUX session. Chunk sizes and polling times are bounded.

For Genesis recovery, add `--initial FILE` with the matching original initial
RAM image and supply both primary and station files:

```text
Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --backup-dir Work:P2Backups
```

This selects the original DOS host-programming path. Its x8/x16 double latch
and RAM sentinel establish Genesis access.
Both true byte-write lanes must pass a reversible RAM scratch test before the
original initial code is loaded and verified. The SRAM-width-specific handoff precedes
JEDEC identification and programming. AMD 01:6E, ST 20:23, SST BF:D5 and
Atmel 1F:04/05 select the corresponding DOS command addresses. The engine checks
the complete old flash, issues one erase, programs each non-FF byte once and
compares every physical flash byte with the intended image. Polling is bounded.

It then takes one full 8 MiB after-capture, reopens and hashes it, and
compares the entire exposed interval with the intended image repeated across
the card's 128 KiB aliases. All phases retain the same insertion ownership.

`verify` takes fresh full before/after captures around INIT and NIC/PRI/STA RID
queries. It requires exact running identities and the complete intended flash
contents. Use it after a cold boot to establish that the updated firmware runs.

## Results and recovery

Keep `before-read-1.bin`, `after-read-1.bin`, both phase manifests and
`transaction.json`. Programming
also saves `planned-flash.bin` and `flash-plan.txt`. These files contain private
card identity/calibration data; do not publish raw captures as example files.

A failed erase/program or required after-readback retains the live RAM session
and card ownership, and writes the failure journal before waiting. Keep power
on and preserve the output. There is no automatic erase, rewrite or reset retry.
Ctrl-E explicitly releases ownership without resetting; it is a recovery
decision, not a success result. `recovery-release.json` records the actual
cleanup. Never cold-boot an unverified partial flash.

See [hardware results](HARDWARE-TESTING.md) and the
[original DOS derivation](DOS-REFERENCE.md).
