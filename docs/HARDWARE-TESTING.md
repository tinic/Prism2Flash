# Deferred hardware testing

No steps in this file have been run. The A1200 is occupied by another project.
Wait for the user's instruction that it is available; do not connect to it or
deploy anything in the meantime.

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

## First session

1. Transfer the native binary only after the machine becomes available. Save
   its checksum and full serial/Shell log. Ensure pccard.library 2.2+ is present.
2. Run `Prism2Genesis info`. Check the IDs against the actual card. This command
   reports CIS IDs only, not an inferred model or tested compatibility claim.
3. Run `Prism2Genesis inspect`. Check mapping offsets, known firmware versions
   and PDA validation. Stop on a mismatch, unsupported card or timeout.
4. With SRAM width documented, run **one** appropriate probe:
   `Prism2Genesis probe --sram 8` or `Prism2Genesis probe --sram 16`.
5. Require a matching four-byte readback, successful normal-mode cleanup and
   responding primary firmware. Capture failures without blindly trying the
   other SRAM width or a different configuration base.
6. Exit the utility, then confirm that the normal driver can claim the card and
   that its identity, firmware and networking still behave as before.

Readback alone is a limited checkpoint. Do not claim full Genesis recovery
until a matching RAM bootstrap executes and answers primary-firmware RIDs.

Later sessions should cover the other SRAM width on appropriate hardware,
older card.resource versions, cancellation and driver-ownership conflicts.
Do not deliberately pull the card during a write as a routine test.
