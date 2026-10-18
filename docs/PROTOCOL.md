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
