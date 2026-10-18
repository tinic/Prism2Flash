# Roadmap

## Current goal: upgrade the actual A1200 MA401

Finish when the selected PRI 1.1.1 / STA 1.7.4 pair runs after a cold boot,
complete saved readbacks and running identities verify it, and Claude receives
the results and A1200 for the dependent driver tests.

- [x] Reverse engineer original DOS ILHOPFW.EXE 0.5e: double latch, detection,
      initial-image loading, low-07 handoff and SST host erase/program sequence.
      DeepSeek independently reviewed the derivation and implementation.
- [x] Implement owned native transactions with persistent destination locks,
      automatic local or explicit UTC timestamps, full captures, close/reopen/hash verification,
      sticky removal and exact CIS/PDA profile binding.
- [x] Qualify actual MA401 x16 Genesis, both true RAM byte-write lanes, all
      initial-code regions and NV JEDEC BF:D5 with four unchanged full captures.
- [x] Prepare the selected pair from the fresh actual PDA, preserve all gaps,
      CIS and PDA, save/reopen the intended 128 KiB image and pin its hash.
- [x] Program the actual card once; compare every physical flash byte and obtain
      two complete 8 MiB after-readbacks matching the intended image exactly.
- [x] Verify cold-boot running NIC/PRI/STA identities and a fresh full capture.
- [x] Hand verified results and A1200 back to Claude (23:10 UTC).
- [ ] Finish the matching committed release and remove task staging.

The full 8 MiB AUX NV layout is accepted for this qualified card; do not reopen
that question. Fresh timestamped full backups before and after impactful work
remain mandatory. Saved manifests never authorize a different insertion.
See [hardware evidence](docs/HARDWARE-TESTING.md),
[DOS derivation](docs/DOS-REFERENCE.md) and [native commands](docs/NATIVE-FLASH.md).

## Remaining release work

- Qualify reliable native full captures around INIT with the running MAC.
  Existing programming/content/self-boot evidence is preserved; do not reflash.
- Finish the local-time quick start, package the exact committed binaries and
  corresponding source, integrate and publish, then remove task build debris.

## Implemented components

- C++23 protocol, checked memory/storage wrappers and native Amiga backend.
- Bounded S-record/metadata parsing, compatibility validation, PDA plugging,
  DOS optional-field defaults, signature stamping and PRI/STA preparation.
- Separate raw initial-image parser and verified Genesis RAM loader.
- Narrow native SST BF:D5 flash engine and full-content/post-boot verifier.
- Persistent failure journals and ownership/RAM retention after programming or
  required readback failure. Explicit reset-free release is recorded separately.
- Host fault models and Amiberry/Copperline startup checks; no FS-UAE.
- Versioned binaries, corresponding source, pccard sources/notices and hashes.

## Subsequent work

1. A regular flasher using a qualified running-primary download/program path,
   sharing the image validation, backups and verification contracts.
2. Explicit restoration/recovery commands with independently proven admission;
   do not assume a saved old file authorizes a different or damaged card.
3. Separately qualify other firmware pairs, card revisions, SRAM widths, NICID
   variants and flash manufacturers. A marketing model name is insufficient.
4. Damaged CIS, early HFA3841, multifunction/ConnectPlus and Prism 3 SSF require
   distinct profiles and evidence. No generic force path.
5. Spare-card recovery experiments need a proven independent recovery route.
   Power-interruption tests require an explicit decision and expendable hardware.

The goal remains open through real failures and repairs. Emulator tests,
commits and release artifacts alone do not establish working upgraded hardware.
