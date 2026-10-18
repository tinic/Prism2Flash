# Hardware testing

The user made the A1200 available on 2026-10-18 and requested that it remain
reserved for Prism2Flash. The first read-only baseline is complete. Existing
backup prerequisites still prohibit active inspection, reset, Genesis entry
and programming until full-flash backup support has been verified.

## First real A1200 baseline — 0.2.1-alpha.1

Published binaries were staged in a unique RAM drawer and downloaded again to
verify their release SHA-256 hashes before execution. The machine reports
Kickstart 40.68, Workbench 40.42, card.resource 40.4, prism2.device 2.9 and
pccard.library 2.2. Its wired management interface stayed online; the Prism2
network interface was unattached. No system configuration or libraries were
changed, and no reset, initialization or firmware command was issued.

All 15 checks passed with an 8192-byte Shell stack: both help/version commands,
two CIS-only reads, all three native backup-required refusals, the full Amiga
version identifier, and five offline synthetic-image checks (inspection,
flash/RAM/pair validation and checksum rejection). The synthetic card/PDA
fixtures do not represent the inserted card. Detailed outputs and exact binary
hashes are in [the evidence](evidence/a1200-readonly-0.2.1-alpha.1.json).

The actual card reports CIS **000b:7300**. The handoff identifies it as a
Netgear MA401RA / ISL37300P. Linux's
[Prism2 driver table](https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/net/wireless/intersil/hostap/hostap_cs.c)
contains this ID, and the
[original MA401RA entry](https://www.mail-archive.com/netdev@vger.kernel.org/msg11668.html)
identifies the same chip string. The ID is consistent with that Prism-family
identification. Our small configured-card allowlist is incomplete; it is not
a chipset detector. Adding the ID alone would not approve a flash map, SRAM
width, NIC/platform tuple, image pair or programming operation. Those remain
open, as does acquiring the first full flash backup.

Open version-display issue: `Version FILE ... FULL` recognized the full
`0.2.1-alpha.1` identifier but displayed the date as `05/13/21`. The binary's
literal tag contains `18.10.2026`. Investigate legacy date-format compatibility
before treating the OS-rendered date as correct. The CLI version is correct.

## Mandatory full-flash backups

Every impactful operation must have its own fresh, timestamped full flash
backup before it starts. This includes active `inspect` (initialization and
cleanup reset), Genesis probes/resets, RAM bootstrap loading, firmware updates,
and restores. Offline file inspection/validation and CIS-only `info` do not
change firmware or reset the card and need no flash backup.

**Current enforcement:** native `inspect` and `probe` refuse before opening the
slot because full-flash export is not implemented. The host protocol simulator
can still exercise the modeled sequences. Versions through 0.2.0-alpha.1 do
not enforce this new requirement; do not use their active commands on cards.
Only the read-only baseline above has run; active tests remain blocked.

The full-backup milestone must implement all of the following before these
native operations are enabled again:

1. Hold exclusive ownership and bind the transaction to this insertion of this
   card. Determine an independently supported physical flash map and size; never
   assume that a generic address interval or readable AUX memory is the entire
   flash. Include PRI, STA, calibration/PDA, CIS where stored in flash, and every
   remaining physical flash byte. Record address-space selection, all ranges,
   map/profile identity, byte ordering, and unmapped/aliased regions. A partial
   PDA/CIS export is not a full firmware backup.
2. Acquire the initial backup without initialization, reset, Genesis entry or
   loading firmware first. Only the minimum configuration/AUX register access
   needed for a verified nonvolatile read is allowed during acquisition, with
   documented cleanup that does not reset the card. The current configuring
   backend resets on close and must be adapted before reuse for backups. If a
   card cannot be read in its current state, refuse the impactful operation;
   there is no force/skip-backup path or automatic recovery exception.
3. Read the full flash twice and require identical bytes. Stream bounded chunks
   with deadlines and sticky removal/cancellation handling. Refuse unknown maps,
   short reads, mismatches, timeouts and plausible-looking data that cannot be
   established as the requested physical flash. Never reset/retry blindly to
   obtain a dump. Bind any available NIC/MAC/CIS identity without introducing an
   initialization command just to retrieve it.
4. Save raw bytes and a manifest in a user-selected persistent directory. Use
   distinct, non-overwriting names such as
   `Prism2-<card>-<UTC timestamp>-<sequence>-before.bin`, and corresponding
   `after` or `failed` names. Record start/end timestamps, clock basis, operation
   ID/type, tool version/source commit, observed identity, ranges, total size,
   SHA-256, and whether acquisition/comparison/file verification completed.
   Check the system clock before relying on a timestamp; never label uncertain
   local time as UTC or silently replace an existing backup.
5. Complete writes, flush/close as supported by the filesystem, then reopen the
   saved dump and verify its exact length and SHA-256 against the read data.
   Save the completed manifest before allowing the operation. Storage errors,
   insufficient space, invalid time, incomplete verification or missing manifest
   block the operation. Filesystem flushing is not a promise against power loss.
   Previously saved files and bare success flags cannot bypass this process.
6. After the impactful operation, acquire and save another timestamped full
   readback and compare it with the initial backup. For RAM probes/bootstrap,
   require unchanged flash. For flashing/restoring, verify the intended physical
   changes and preservation of all other regions. A post-operation acquisition
   failure or unexpected difference makes verification fail; retain the initial
   backup, logs and any clearly marked partial data. On operation failure, also
   attempt a separate full readback only if the current card/session remains
   accessible without an extra reset, unsafe retry, or loss of RAM recovery.
   Never substitute another inserted card, report a partial read as complete,
   or perform an automatic restore merely because verification failed.

Backups contain card-specific calibration and identifiers. Preserve them with
all transaction records; do not commit or redistribute them as sample firmware.
A complete backup is useful recovery evidence, but does not itself establish
that the still-unimplemented restore path can recover a card.

Flash has reported static Linux-driver evidence for AUX nonvolatile reads and
post-program comparison. Its exact address-space handling, physical maps and
read-only acquisition/cleanup still need independent source review and eventual
hardware validation. No fixed full-card dump interval is approved yet.

## Record before testing

- Amiga model, accelerator, CPU, Kickstart/Workbench and card.resource version.
- Exact card labels, revision and FCC ID; photos can be collected during testing.
- CIS manufacturer/product IDs, NICID/variant, current PRI/STA versions.
- Source establishing the card's SRAM width and matching Intersil memory map.
- Network driver/manager state; release their slot ownership before using the tool.
- pccard.library version (minimum 2.2; early versions had 68000 alignment bugs).

Use a spare supported 5 V-capable 16-bit PC Card. Keep a separate known-good
recovery machine/tool available before later flash tests. The first experiment
does not program flash, but it resets the card and changes SRAM.

## Future first active session (blocked pending full backups)

1. Transfer the native binary only after the machine becomes available. Save
   its checksum and full serial/Shell log. Ensure pccard.library 2.2+ is present.
2. Run `Prism2Genesis info`. Check the IDs against the actual card. This command
   reports CIS IDs only, not an inferred model or tested compatibility claim.
3. Only after the full-backup milestone and its independent verification are
   complete, run `Prism2Genesis inspect` with mandatory before/after dumps.
   Check mapping offsets, known firmware versions and PDA validation. Stop on a mismatch, unsupported card or timeout.
4. With SRAM width documented and a fresh verified full backup saved, run
   **one** appropriate probe:
   `Prism2Genesis probe --sram 8` or `Prism2Genesis probe --sram 16`.
5. Require the timestamped post-operation full readback to match the initial
   backup, along with a matching four-byte RAM readback, normal-mode cleanup and
   responding primary firmware. Capture failures without blindly trying the
   other SRAM width or a different configuration base.
6. Exit the utility, then confirm that the normal driver can claim the card and
   that its identity, firmware and networking still behave as before.

Readback alone is a limited checkpoint. Do not claim full Genesis recovery
until a matching RAM bootstrap executes and answers primary-firmware RIDs.

Later sessions should cover the other SRAM width on appropriate hardware,
older card.resource versions, cancellation and driver-ownership conflicts.
Do not deliberately pull the card during a write as a routine test.
