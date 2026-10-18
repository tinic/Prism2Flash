# Hardware testing

The actual A1200 Netgear MA401 V2.5 / PD5LMWP200 is the qualified target.
On 2026-10-18 it passed x16 Genesis, original initial-code loading, SST BF:D5
identification, programming and complete intended-image readback. Cold-boot
NIC/PRI/STA identities also matched without a host RAM bootstrap. The slot was
released and Claude received the firmware results and A1200 at 23:10 UTC.
See [the sanitized report](evidence/ma401-upgrade-2026-10-18.json).

## Native programming — 2026-10-18

- Bootstrap qualification: `fa2f459`, transaction `20261018T221517Z`.
  Both byte-write lanes and all 3434 initial-code bytes passed. Four full
  saved/reopened 8 MiB captures matched the untouched original SHA-256
  `f818d628d47067b3e111476192988ca85c868e22dc52f9b2a2fa4d87f0398a9f`.
- Programming: `be2215b`, transaction `20261018T223118Z`, 16 KiB guest stack.
  Fresh before captures matched that original hash. SST BF:D5 erase/program
  passed, with 57,802 non-FF bytes programmed and all 131,072 physical bytes
  verified against the intended image.
- The immediate full after-capture failed at zero bytes: final word verification
  left AUX NV selected, obscuring CONTROL. The tool persisted failure and held
  card/RAM ownership. After diagnosing this exact state, explicit Ctrl-E
  released without reset; the previously qualified scratch cleanup changed
  CONTROL from 6500 to C000 and restored CIS 000b:7300. No flash command,
  rewrite or reset was issued during recovery. Original failed manifests remain
  failure evidence, not a retrospectively successful transaction.
- Completed after-readback: `20261018T223627Z`, two saved/reopened 8 MiB files.
  Both SHA-256 values were
  `043064358631045a04efecd6d13a05722ac9eaeb24902b7e88c9616116819598`.
  Every exposed byte matched the intended 128 KiB image repeated across aliases.
  CIS/PDA and all gaps were preserved. Intended image SHA-256:
  `c86451112ad242c9d375260d8113f848b83e77b86d8c48e4ec6f7be49725cd96`.
- Fix `2bfbbd4` clears AUXOFFSET after the flash engine's final word reads;
  its regression checks normal register visibility before the next capture.

## Cold boot and handoff — 2026-10-18

- SyncReboot flushed the system before one power cycle. The wrapper's readiness
  ping failed because playhouse2 lacked NET_RAW; independent HTTP 200 and an
  Amiga command exit 0 confirmed startup. No second power cycle was issued.
- `0d5622b`, run `20261018T224923Z-verify`: the fresh full 8 MiB before capture
  matched `04306435...`. INIT and all expected identities passed:
  NIC `800c/0/1/0`, PRI `0015/1/1/1`, STA `001f/4/1/7`.
  No initial image was loaded by the host. This establishes self-booted
  PRI 1.1.1 / STA 1.7.4; it does not establish WPA2 association or traffic.
- That verifier's after-capture failed before any saved bytes because consecutive
  NV pages disagreed. Later attempts retained every partial file and failure;
  `0c8cd0d` timed out after 240 seconds and 4,186,112 bytes, before INIT.
  Cleanup completed and no recovery hold remained. There was no extra erase,
  rewrite or reset in these attempts.
- Sustained NV capture with the running MAC remains a tool qualification issue.
  Offline follow-up replaces per-word GetSysTime conversion with an E-clock
  wait and fails on an unexpected AUX disable. The 240-second deadline,
  consecutive-page agreement, all-alias check and intended-image comparison
  remain enforced. Hardware timing of that follow-up is not yet established.
- At 23:10:49 UTC, Claude was notified that the A1200 was available for dependent
  driver tests, with exact firmware identities, complete earlier readback hash
  and the additional verifier limitation.

Private evidence retains all original, partial and completed raw files,
commands, exact binaries, hashes, phase manifests and cleanup records under
`/Users/turo/ai/evidence/prism2flash/hardware/`. Raw card identity/calibration
and proprietary input files are not distributed. See
[NATIVE-FLASH.md](NATIVE-FLASH.md) for current commands and restrictions.

The following sections are chronological records of earlier releases and
experiments; their then-current admission holds do not replace the latest
qualified card profile above.

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
slot because physical full-flash backup coverage and its operation gate are
not verified. Experimental AUX address-space capture is separate from that gate. The host protocol simulator
can still exercise the modeled sequences. Versions through 0.2.0-alpha.1 do
not enforce this new requirement; do not use their active commands on cards.
The baseline above and experimental acquisition below have run; active tests
remain blocked.

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
   documented cleanup that does not reset the card. The regular configuring
   backend resets on close; the separate acquisition backend must not use that
   cleanup path. If a
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

## Experimental AUX NV capture — 0.3.0-alpha.1 development

The user explicitly requested a real backup. The acquisition command matches
both MANFID 000b:7300 and all four MA401RA/ISL37300P VERS_1 strings. It reads
AUX NV selector 1 without INIT, Genesis, AUX payload writes or flash commands.
It does not require adding ambiguous NICID 800c to the active-card allowlist.
The observed mapping is attribute offset 0x3e0, I/O offset 0, index 1.

The first capture at 20261018T104136Z saved 8,388,608 raw bytes, SHA-256
`c37ba9bb1294f89fbbf62cea6c65cc839f145f97ba8d08df024c47e6b9221588`.
The native hash matched the downloaded Mac file. The second-pass comparison
and scratch-register cleanup verification failed. A subsequent attempt could
not read MANFID and stopped before configuring or accessing AUX. Preserve the
raw capture and failure manifests; this is not a verified physical-flash backup.

Offline analysis independently repeated by DeepSeek found 64 near-aliases of a
128 KiB window. Each alias differs from byte consensus by 23–71 bytes; 59/64
PDA copies pass the actual PDA CRC. A separately retained majority-derived
128 KiB analysis image has SHA-256
`5f63f69147433e26210a9a91ed2938a6913e1491bae0a4ecf0a8483db4707b8f`.
It is derived evidence, not an untouched successful acquisition. Geometry is
inferred from aliasing; neither flash JEDEC/CFI identity nor complete physical
coverage has been established. Firmware strings identify PK010007.HEX and
SF010306.HEX; alignment and strings alone do not verify every firmware byte.
Actual dumps, CIS/PDA contents and MAC/calibration stay outside public source.

This exposed an earlier PDA parser defect: the polynomial must be reflected
0xa001 (CRC-16/ARC, initial zero), not 0x8408. The retained Host AP util.c table
and linux-wlan-ng implementation agree; the real PDA stores CRC 0x82a2 and its
full-record residue is zero. The old synthetic fixtures shared the defect.
An independently pinned original synthetic CRC vector now catches it.

The user approved a one-time restart exception after the failed initial
verification. The existing flush wrapper selected SYS:Unpacked/SyncReboot;
its ICMP readiness check reported not_back, while separate HTTP Shell access,
a fresh RAM drawer and a successful CIS recheck established machine recovery.
No power-cycle command was used. This exception does not change the backup
contract or add a CLI override. CIS again reports 000b:7300.

The reader now clears AUXOFFSET before closing AUX, disables only a port it
opened itself, and never replays opaque scratch-register values after disable.
AUXPAGE/OFFSET read back 0xffff on this card and cannot be treated as restorable
settings. On the fresh boot CONTROL already reported AUX enabled; the reader
leaves that enable state intact under exclusive card.resource ownership.
PARAM scratch values are zeroed only by the normal AUX disable sequence.
Cleanup status, backend COR restoration and raw register diagnostics are
recorded separately. The paced attempt at 20261018T105630Z saved another full 8 MiB raw pass
(SHA-256 `1b739b7271f8e0856c8861dbef7c35a78fb60077ebfe05ac2f363cdf76a1d702`)
and 1,011,712 bytes of pass two before the 240-second deadline. There were 723
comparison differences. Both cleanup checks passed and subsequent CIS remained
readable. The new raw pass reconstructs to exactly the same analysis-only
128 KiB consensus as before the restart. Two-microsecond inter-word pacing did
not reduce errors and was removed. The current reader instead requires two
consecutive identical whole-page reads, at most twenty reads per 128-byte page,
and still compares both complete files. It counts disagreement/retry evidence
and refuses persistent instability rather than voting on bytes. All attempts remain timestamped
and retain their partial files. The A1200 remains reserved for this project.


### Successful repeated NV acquisition — 20261018T110206Z

The bounded page-agreement reader completed in 182.050 seconds, including
saved-file verification. Both complete 8,388,608-byte passes are byte-identical,
SHA-256 `f818d628d47067b3e111476192988ca85c868e22dc52f9b2a2fa4d87f0398a9f`.
Each saved file was reopened and verified on the A1200; downloaded Mac files
also match that hash. There were 271,975 page reads and 9,831 disagreement
comparisons; no byte voting/repair was used. The acquisition and both cleanup
statuses are zero. A subsequent CIS-only read again returned 000b:7300.

All 64 captured 128 KiB windows are now exactly identical. An unaltered slice
at NV 0x7e0000–0x7fffff hashes to
`5f63f69147433e26210a9a91ed2938a6913e1491bae0a4ecf0a8483db4707b8f`,
exactly matching both earlier offline consensus reconstructions. The successful
raw files and all earlier failed/partial attempts remain on persistent Amiga
storage and the Mac. DeepSeek received a private hash-verified copy for offline
analysis. [Sanitized acquisition evidence](evidence/nv-capture-0.3.0-alpha.1.json)
contains source/binary hashes, manifests and limitations, without dump payloads.

This verifies the complete exposed AUX NV address-space capture on this card.
Physical geometry/map coverage is still inferred. It does not satisfy the
remaining independent physical-map/operation-gate requirements and does not
authorize INIT, reset, Genesis, bootstrap, flashing or restore. No further
reset was used after the approved recovery exception.
