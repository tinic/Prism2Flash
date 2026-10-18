# Genesis and eventual firmware update plan

## Current checkpoint

Verified RAM transfers and offline private-copy preparation are implemented.
No native active inspection/probe is enabled. No DOWNLOAD/PROGRAM command 0x22
or executable bootstrap loader is present. Model and emulator results cannot
approve a physical card or establish recovery after a failed flash.

The user's supplied label identifies FCC PY3MA401RD, 5 V. Earlier captures used
MA401RA, the observed CIS text, as their evidence-directory name. Preserve those
names/hashes; do not silently reclassify the captures as a complete physical
flash backup. The two 8 MiB captures establish repeatable AUX NV data, with
128 KiB aliasing, but their manifests correctly withhold physical coverage.
The saved CRC-valid PDA's configuration PDR 0x0400=0x0003 points toward x16
SRAM according to Host AP's bit assignment. Record it as a card-specific clue
awaiting independent corroboration; it does not identify flash geometry.
The user-supplied FCC photos now corroborate a candidate 128 KiB SST39VF010
flash on the filing sample. Its apparent part marking and the captured alias
period need independent profile/address-map review before the operation gate
can treat them as proof of full coverage on the user's unit.

## Gates before a real Genesis probe

1. Establish the exact card profile: PCB/chip evidence or equivalent primary
   documentation, NIC/variant binding, SRAM width, memory decode, physical flash
   extent and every flash/CIS/PDA/calibration range. Inspect the exact FCC filing
   photographs when retrievable. Do not select x8/x16 from the host bus label,
   generic CIS strings, firmware filename, NIC800c alone or alias period alone.
2. Independently review the physical map against AUX addressing. Prove that a
   complete capture covers all nonvolatile bytes without invisible banks. An old
   raw capture or an editable manifest cannot authorize an operation.
3. Implement one owned operation session: acquire exclusive slot ownership,
   save a fresh timestamped full physical capture, flush/close/reopen and verify
   every saved byte before any initialization/reset/probe. Bind capture, card
   identity and exact profile within that session. Refuse stale, incomplete,
   removed/reinserted, failed or volatile-storage backups. No force/skip path.
4. Test storage errors, removal, cancellation, changed identity, unstable reads
   and every phase boundary offline. Demonstrate refusal before side effects on
   all gate failures. Independently verify the gate before enabling native use.
5. Run the bounded Genesis RAM probe on the reserved A1200 under that gate.
   Use the proven SRAM width, verify expected bytes, return to the known normal
   state and capture the timestamped full post-operation readback. Compare all
   physical flash bytes and identities; a RAM probe should leave flash unchanged.
   Keep ownership through restoration and post-readback. Do not perform another
   implicit backend reset after the final readback. Preserve all failures.

The user has requested Genesis testing. The remaining backup/profile gates
currently prevent the physical test; emulator checks can continue immediately.

## Bootstrap after Genesis is proven

Export prepared payloads with source/card/PDA/segment provenance, checked closes
and independent reopen verification. Bind each region to approved RAM ranges,
load through the verified write-once helper, verify all bytes, start the known
matching RAM primary, and confirm its RIDs. Keep the slot and recovery session
alive; a short-lived CLI's normal reset/release would discard the RAM primary.
This needs its own fresh full before/after capture transaction.

## Eventual nonvolatile flash engine

Build the complete card-specific PRI/STA update in RAM before erasing anything.
Keep primary-only regular flashing prohibited. Verify download-buffer RID FD01,
staging capacity, allowed physical destinations and preservation ranges. Implement
command 0x22 with bounded staging/program phases and hardware-appropriate long
deadlines, following retained Host AP source and independent verification.

Carry the read-path lessons forward: exact expected-byte checks, consecutive
whole reads for noisy readback, no voting or synthesized bytes, no automatic
rewrite/reset/reclaim, sticky removal and explicit operation/cleanup errors.
Programming failure must retain the last completed phase/block and preserve a
RAM recovery session where possible. Preserve backups, partial captures and logs.

After programming, obtain a timestamped full physical readback, compare every
changed region against prepared bytes and every untouched region against the
fresh pre-operation backup, then verify normal identities. Verify recovery on a
spare with a proven independent route before claiming a usable recovery tool.
Power-interruption experiments need an explicit separate decision and expendable
hardware. Association/security tests belong to a later firmware acceptance run.
