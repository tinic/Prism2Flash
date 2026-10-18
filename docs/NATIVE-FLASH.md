# Native MA401 firmware update

**Start here: [quick start](QUICKSTART.md).**

`Prism2Flash` is a native Amiga command. `Prism2Flash --help` and
`Prism2Flash --version` do not access the card. Use `Stack 16384` for hardware
operations. Firmware files are not distributed with this project.

The current programming path is restricted to the qualified Netgear MA401
V2.5 / PD5LMWP200: exact MA401RA/ISL37300P CIS, NIC 800c/0/1/0, x16 SRAM
configuration and runtime SST BF:D5 identification. A matching product name
alone does not approve another card. Generic dead-card recovery is not exposed.

## Simple commands

Use `--backup-dir EXISTING_DRAWER`. The tool creates a fresh subdrawer,
saves the before/after captures and checks their hashes automatically.
**No clock setup is required.** Dates are best-effort labels only. If the clock
is unreadable, the label is `untimed`; duplicate labels get a numeric suffix.
An unset, incorrect, frozen or changing clock cannot block flashing.

For a chosen output name, use `--output NEW_DRAWER` instead:

```text
Prism2Flash program --initial ID010001.HEX --primary pk010101.hex --station sf010704.hex --output Work:P2Backups/Upgrade1
Prism2Flash verify --primary pk010101.hex --station sf010704.hex --output Work:P2Backups/Check1
```

The parent must exist and have 20 MiB free. Existing output drawers are refused
to preserve backups. Legacy `--utc`/`--utc-offset` arguments affect labels only;
they are optional and never validate the system clock.

`--expected-plan-sha` additionally pins the entire prepared 128 KiB image. The
image includes this card's own PDA, CIS and unchanged gaps, so its hash is
card-specific. The option is accepted by both commands; saved hashes never
authorize another insertion or replace fresh captures.

The initial bootstrap hash is required. The PRI/STA hashes below identify the
tested pair; other compatible PRI/STA images pass the same metadata, platform,
interface, PDA and address checks. Their hashes are recorded automatically:

| Input | Component | SHA-256 |
|---|---|---|
| ID010001.HEX | Original DOS initial RAM code | `1d326cee5887005e9b755e74e5578ccfbd74ae5e7ebc19c1af23e61b7a76c018` |
| pk010101.hex | PRI 1.1.1 | `20638ee6180583488f769d95ed0b240b115bb8b8f6a72e2597dd72d58b4b7791` |
| sf010704.hex | STA 1.7.4 | `92e3101220003bed0caeebe42d0817e96db67b1541c62c7601e7772f1c6aa3e5` |

## What programming does

The utility claims the slot exclusively and takes one complete 8 MiB AUX NV
capture. It checks all 128 KiB aliases for exact equality, then closes, reopens
and hashes the saved file before any Genesis or
flash work. Preparation uses the PDA from that same fresh capture. It overlays
only validated image segments onto the complete old 128 KiB image, preserves
the CIS/PDA interval and every other gap, saves the intended image, reopens it
and checks its hash.

The DOS-derived x16 double latch and RAM sentinel establish Genesis access.
Both true byte-write lanes must pass a reversible RAM scratch test before the
original initial code is loaded and verified. The low-07 handoff precedes
SST identification and programming. Only BF:D5 is accepted. The engine checks
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
[original DOS derivation](DOS-REFERENCE.md). The release supports this narrow
qualified path; other cards and restoration remain separate work.
