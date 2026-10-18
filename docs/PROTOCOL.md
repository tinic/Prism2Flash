# Genesis probe implementation

## Transport contract

`p2_bus` uses host integers for register values and byte arrays for payloads.
The Amiga backend swaps 16-bit register values exactly once; the AUX layer
assembles/disassembles payload words explicitly. It never dereferences an
unaligned `uint16_t *` into a file or PDA byte buffer.

The Amiga backend obtains mappings from GetCardMap. The configuration base
and COR/CCSR offsets decoded by pccard.library are already in mapped
attribute-memory units. Do not double them again. This follows the upstream
Amiga driver and still needs verification against the physical target card.

Only the first CFTABLE entry with its own 64-byte I/O window is supported at
present. Inherited/default configurations and multifunction cards are refused.
INFO reads only the MANFID tuple; other commands decode/configure the window.

Ownership uses CARDF_IFAVAILABLE. Removal is sticky; no insertion handler
reclaims a replacement card. CardResetCard and ReleaseCard run in task context.
The native backend guards each MMIO operation against the removal flag; this
cannot make physical hot-removal electrically safe or recover an interrupted
write. It stops further software access once notified.

## Sequence

1. Disable Prism interrupts and issue INIT; poll BUSY and command completion
   with deadlines. Check STATUS before acknowledging the completion event.
2. Read NICID, PRIID and (if available) STAID through BAP0. Refuse unsupported
   NICIDs before the Genesis RAM test.
3. Unlock AUX with PARAM0=FE01, PARAM1=DC23, PARAM2=BA45, and CONTROL bits
   15:14. Wait for enabled status. Read 1024-byte PDA candidates at 7F0000,
   3F0000, 390000 and 7F0002; validate lengths, end marker and CRC.
4. Save COR/CCSR for the diagnostic log. Toggle COR software reset with 2 ms
   delays and write Host AP's sentinel `00 E1 A1 FF` at AUX address 7E0038.
5. Assert COR reset, wait 10 ms, write CCSR=1F (x8) or 0F (x16), wait 10 ms,
   release COR reset, and wait 10 ms.
6. Read the sentinel, write it again and read back. Compare all four bytes.
   This intentionally follows Host AP's limited RAM-access test; it is not a
   full SRAM test or proof that a recovery firmware image will execute.
7. Return through the same reset sequence with CCSR=17 (x8) or 07 (x16).
   Disable AUX. Cancellation does not suppress this cleanup, but removal does.
8. Initialize and read PRIID again. On process exit, the native backend resets
   the configured card and releases its handle, then closes timer/dependencies.

AUX uses PAGE=address>>7 and OFFSET=address&7F. Transfers require even addresses
and lengths, stay inside the 23-bit AUX space, and explicitly reselect the
page at every 128-byte boundary.

The probe requires existing primary firmware. Recovery without functioning
firmware is a later backend/profile path, not an implied feature of this code.
No flash download command (22) is accepted by the core command helper.

## What the model establishes

The model checks protocol sequencing and fault handling. It cannot establish
Gayle byte lanes, the real CCSR interpretation, electrical compatibility,
firmware execution, or recovery after an interrupted flash. Host tests and a
native build must not be used as substitutes for those hardware checks.

## Experimental NV acquisition

The separate `backup` path uses AUXOFFSET selector 1 (`0x1000`), a 16-bit
AUXPAGE and explicit 128-byte page boundaries. It reads all 23 address bits
twice into distinct files and compares the entire address space. Each 128-byte page requires two consecutive identical whole-page reads,
with at most twenty reads per page per pass and the same total deadline.
Disagreements are counted in the manifest. No bytes are voted on or repaired;
only a complete raw page that matched its immediately preceding read is saved.
The two independently acquired full files must also match byte-for-byte.
Persistent instability fails the capture. Inter-word timer pacing was removed
after it slowed real acquisition without reducing errors.

This path never calls INIT, Genesis, AUX payload writes, or DOWNLOAD. Exclusive
card.resource ownership and exact MA401RA CIS strings are required. It refuses
a COR reset bit or a different active I/O configuration; a temporary COR
selection is restored and checked without CardResetCard. AUX remains enabled
if it was already enabled. Clear AUXOFFSET while accessible, then disable only
an AUX session opened here, using PARAM0/1/2 zero as Host AP does. Do not replay
old AUXPAGE/OFFSET or PARAM values: pointer-register readback may be 0xffff and
is not evidence of a restorable setting. Before/after values are diagnostic.

A 240-second acquisition deadline, sticky removal and cancellation checks
bound the protocol. Cleanup ignores cancellation while still honoring removal.
Files are flushed, closed and independently reopened for exact size/SHA-256
verification. The manifest always withholds physical coverage and authorization
for impactful operations until the card profile and backup gate are verified.
