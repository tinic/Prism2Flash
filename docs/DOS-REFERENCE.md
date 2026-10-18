# Original DOS utility reference

The user requires original DOS ILHOPFW.EXE to be the reference for Genesis,
bootstrap and programming. Host AP and prism2.device corroborate the transport.

- Intersil PRISM Flash Utility **0.5e, Apr 24 2002**, Intel v1.0.7.
- EXE: 157300 bytes, SHA-256
  `f7e2ec28585dc17315983e7f10bb44874811fd58c751f818efc9f38bb8a9bbe5`.
- [Original mirror](https://junsun.net/linux/intersil-prism/dos-resurrection/):
  matching EXE/INI, `prismdos.txt`, `DOScd.txt`, `ID010001.HEX`.
- Complete paginated `ILHOPFW -?` executed in DOSBox 0.74-3-3 and captured.
- Original executable and firmware stay in private research evidence.
- DeepSeek Flash was explicitly assigned binary reverse engineering.

MZ load starts at file **0x2a00**. Hardware segment **0a22** has file base
**0xcc20**; card-I/O segment **1960** has file base **0x1c000**.

| Original location | Verified observation |
| --- | --- |
| Help `-3842 <GenNor>` | High byte selects Genesis straps; low byte normal straps. `0F07` selects `0x0f` / `0x07`. |
| Help `-i` / `-gen` | `-i` loads an initial image into RAM and sets Genesis; `-gen` selects the subsequent Genesis path. |
| Help `-on` | Keeps card power/configuration after exit, retaining RAM recovery state across DOS invocations. |
| Help `-hf` | Host flash algorithm instead of firmware algorithm. |
| `0a22:3803`, file `0x10423` | AUX seek to `0x7e0038`, write words `E100`, `FFA1`, seek/read/compare. AUX enable and one repeat on initial failure. |
| `0a22:0ad5` | Reset dispatcher uses two Genesis HCR latches with idle seeding/verification between them; normal return uses one latch. Original x86 routine was executed with relocated pointers and stubbed I/O to confirm the sequence. |
| `1960:1ba6` | PC Card COR byte writes at attribute offset `0x3e0`. |
| `1960:1c19` | PC Card HCR byte writes at `0x3e2`; PCI register is `0x5c`. |

Amiga configuration base and I/O window come from CIS; these locate the
registers. They do not alter the DOS literal values written to COR. Socket
power control remains card.resource/Gayle-specific.

## Initial image

The original `ID010001.HEX` is 10372 bytes; SHA-256
`1d326cee5887005e9b755e74e5578ccfbd74ae5e7ebc19c1af23e61b7a76c018`.
It has 218 S3 records, 3434 payload bytes in seven regions, no PRI/STA metadata
and a zero S7 marker. `Prism2Firmware inspect-initial` handles this separate raw
RAM dialect without weakening regular image validation. Zero S7 here is not
flash classification or a generic execution address. Loading still requires
the DOS-derived initial-image start sequence and exact card binding.

## Original x86 reset trace and Amiga adaptation

Executing the unmodified relocated DOS dispatcher with an emulated card-I/O
interface produced this x16 trace (all delays milliseconds):

```text
idle words at 7e0038
COR=C5, HCR=0F, delay 5, COR=45
idle words and exact readback
COR=C5, HCR=0F, delay 5, COR=45
normal return: COR=C5, delay 5, HCR=07, delay 5, COR=45
```

The Amiga probe follows that two-latch sequence, with no preliminary bare COR
reset or native INIT. Native x16 uses the literal COR45/C5 pair. The backend restores the
claim’s prior COR after readback with a non-resetting write. A 5 ms settling interval follows deassertion on
Gayle. Required idle verification uses two consecutive exact reads, bounded at
twenty; it never retries payload writes because of a bad read. Cleanup ignores
Ctrl-C only to return to normal straps and restore the original AUX state;
sticky removal still prevents further I/O. The model records every COR/HCR write
and its timing against this DOS-derived sequence. The final normal firmware
response and unchanged full NV readback remain separate hardware checks.

DeepSeek independently confirmed the dispatcher and original x86 trace. Its
important correction was to retain the literal COR45/C5 values even when the
previous COR is41. The first native transaction was cancelled during its
before-capture phase to incorporate that correction: operation_started=false,
cleanup_error=0, final CIS check passed. No Genesis/reset/INIT occurred. Partial
files and the failure journal were preserved. The direct AUX seek path and DOS
legacy mask/advance workaround address the same windows; the workaround and
setup diagnostics are separately documented in the private RE report.

## First complete native latch test, 2026-10-18 21:46 UTC

Both DOS x16 Genesis latches completed. The sentinel read `00 e1 a1 ff` on
two consecutive reads with no disagreement. The normal INIT timed out while
COR remained at the DOS literal45. Both saved 8 MiB after captures agreed,
but differed from the unchanged dual before captures; CIS stayed readable.
The after data repeated every128KiB and contained a changed byte-access
pattern. No flash erase/program command was issued. Raw failure captures,
manifests and the exact binary are retained privately.

The concrete Amiga transport correction restores the insertion's original
COR (without resetting) immediately after the normal HCR latch, before AUX
cleanup, INIT and NV word readback. Previously the backend restored it only
after readback. This is an access-configuration hypothesis to test on the real
card; passing RAM liveness alone is not a completed normal-return test.

True byte I/O uses Gayle's separate odd-byte bank, as defined by
[Linux v6.17 amigayle.h](https://github.com/torvalds/linux/blob/v6.17/arch/m68k/include/asm/amigayle.h):
an odd port adds0xffff to its offset, making AUXDATA+1 use bankoffset0x1003e.
DOS byte helpers preserve the odd address in AUXOFFSET and select port3f;
the Amiga backend maps that port with a byte cycle, never a word RMW.

## DOS preparation and host writer implementation

DeepSeek corrected its initial copy-direction report by tracing117c:04f6:
matching PDA payloads flow into the firmware image. Missing or mismatched
records retain the entire HEX field, with a warning. No partial PDA field is
copied. `--plug-policy dos` implements that behavior for the twelve optional
fields observed in this pair; RF calibration, identity and regulatory data
remain required. Every fallback is reported. Inactive overlapping plugs do
not write; active overlapping destinations remain rejected. DOS filename
fields are uppercase, bounded by the field and20bytes. The standalone PRI
signature placeholder can be replaced by the STA DE C0 prefix.

`bootstrap::load` splits Genesis entry from exit so raw INITIAL CODE can be
written and independently verified before low07. It sends no INIT/DOWNLOAD.
A failed load retains Genesis and reports recovery required. The native
`bootstrap` command admits only the exact original ID010001 source hash and
uses the same fresh owned full before/after capture transaction. It checks
NV/PDA and JEDEC ID/read-mode restoration without erase/programming.

`flash::program_sst` implements DOS's two ID-unlock attempts, exact SST BF/D5
admission, full128KiB old-image comparison, whole-chip erase, single-write
per-byte equality polling and complete post-verification. Erase timeout has
no continue option. Errors after erase setup retain a recovery-required
result; the caller must retain power/session. No native programming command
is enabled yet. `flash::Plan` overlays only actual prepared bytes on the
complete old image and preserves the CIS/PDA gap; descriptor holes are not
filled. Tests cover preservation, unknown IDs, stale contents, wraparound,
erase/program timeouts, cancellation and post-verification failures.

The first native byte diagnostic established that even-byte reads matched
all512 original PDA bytes, while every odd-bank byte floated FF. The Amiga
backend now uses the established GETODD aligned-word read for the odd byte;
writes retain true byte cycles in Gayle's odd bank. AUX byte seeks align the
word and select the lane. This correction requires physical validation;
word-read PDA after power-cycle matched the original exactly. The diagnostic
also now clears AUXOFFSET while AUX is enabled, as the established backup
reader already does.

Native validation2026-10-18 22:05UTC: aligned GETODD byte reads and word reads
both matched all1024 original PDA bytes exactly (CRCvalid). Original PDA hash
and full raw diagnostics are retained privately. The bootstrap now tests both
true byte-write lanes in unused Genesis RAM, verifies with independent word
reads twice, restores the original scratch word with byte writes and verifies
it again before loading initial code. Failure retains Genesis and prevents
JEDEC identification. Native erase/program was still disabled at that checkpoint.

Native bootstrap2026-10-18 22:07UTC (b129eab): both true RAM byte-write
lanes passed; all3434 initial-code bytes were written and independently
verified; low07 handoff completed; NV PDA remained valid; JEDEC read BF:D5
(SST39VF010), then F0 returned the flash to read mode. No erase/program.
The full after-capture failed at its first register read because the byte
operation left the NV selector active. Completing the missing AUXOFFSET=0
cleanup restored CONTROL=C000 and the exact CIS without any reset or payload
write; subsequent word and byte PDA reads both matched the original. Byte
read/write APIs now clear the selector before returning. A full owned rerun
was required and subsequently passed at 22:15 UTC; failed empty after files remain failure evidence.

`Prism2Flash` now has program/verify commands, exact input hashes, preparation
from the same owned transaction's fresh PDA, a saved/reopened intended128KiB
image, and comparison of every byte in the final8MiB capture against that
intended image. Native program admission is restricted to the qualified MA401 path. Any failed erase,
program or required after-capture retains card ownership and RAM through a
recovery callback, after persisting the failure journal; explicit Ctrl-E
releases ownership without a reset. The caller must not power-cycle an
unverified partial flash. Standalone firmware verification also takes fresh
before/after captures around INIT/RID queries.

## Qualified transaction and review fixes

The complete native transaction fa2f459 at20261018T221517Z passed:
- two Genesis latches and both true RAM byte-write lanes;
- all 3434 initial-code bytes checked: 50 bytes written/verified and 3384 already matching;
- valid original PDA and SST JEDEC BF:D5;
- four complete saved/reopened8MiB captures, all SHA256
  f818d628d47067b3e111476192988ca85c868e22dc52f9b2a2fa4d87f0398a9f;
- exact before/after equality, cleanup/journal0 and readable CIS afterward.

DeepSeek independently reviewed62d51cb/b129eab/fa2f459 against the DOS engine
and ran24 host tests under ASan/UBSan. The required physical byte-write proof
is now present. Its remaining findings are addressed: F0 cleanup ignores only
cancellation after identification errors; post-load AUX is explicitly ensured;
native programming locally requires byte-write and handoff proof; RAM scratch
writes require this live Card's Genesis latch; erase attempt and completed
command are distinct; PDA lookup checks bounds; missing UTC is explicit;
post-hold cleanup gets a separate saved/reopened release record. Typed mock
initialization fixes GCC14's class-memaccess warning without suppressing it.

Native programming is admitted only for the exact MA401 path, with fresh owned
captures and all runtime checks above. Optional expected-plan SHA pins the
freshly prepared128KiB image before bootstrap/erase. The independently
constructed actual-card candidate preserves all gaps, CIS and PDA; its payload
changes58641bytes. Verification checks prepared flash bytes and firmware RIDs.
The actual update subsequently passed physical programming/full-image verification;
see HARDWARE-TESTING.md for the complete run and the final word-read selector cleanup.
