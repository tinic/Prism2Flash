/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "prism2.h"

namespace p2::flash {
inline constexpr uint32_t Bytes = 0x20000;
struct Identity {
  uint8_t manufacturer = 0, device = 0;
};
// JEDEC ID/reset only, never erase/program. Caller still needs fresh captures.
[[nodiscard]] CardResult<Identity> identify(Card &);
struct Report {
  p2_error error = P2_OK;
  uint8_t manufacturer = 0, device = 0;
  uint32_t last_address = 0, programmed_bytes = 0, verified_bytes = 0;
  bool erase_command_attempted = false, erase_started = false;
  bool recovery_required = false;
};
// DOS ILHOPFW -hf algorithm, with no interactive continuation on timeout.
// Caller owns the live card, saved full before capture, RAM bootstrap and
// recovery lifetime. Both buffers must be complete canonical 128 KiB images.
// Initial native profile supports the DOS's SST39VF010 ID only.
[[nodiscard]] Report program_sst(Card &, p2_bus &,
                                 std::span<const uint8_t> before,
                                 std::span<const uint8_t> desired);
} // namespace p2::flash
