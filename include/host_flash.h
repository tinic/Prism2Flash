/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "prism2.h"

namespace p2::flash {
inline constexpr uint32_t Bytes = 0x20000;
struct Identity {
  uint8_t manufacturer = 0, device = 0;
};
[[nodiscard]] constexpr bool supported(Identity id) {
  return (id.manufacturer == 1 && id.device == 0x6e) ||
         (id.manufacturer == 0x20 && id.device == 0x23) ||
         (id.manufacturer == 0xbf && id.device == 0xd5) ||
         (id.manufacturer == 0x1f && (id.device == 4 || id.device == 5));
}
// JEDEC ID/reset only, never erase/program. Caller still needs fresh captures.
[[nodiscard]] CardResult<Identity> identify(Card &);
struct Report {
  p2_error error = P2_OK;
  uint8_t manufacturer = 0, device = 0;
  uint32_t last_address = 0, programmed_bytes = 0, verified_bytes = 0;
  bool erase_command_attempted = false, erase_started = false;
  bool program_command_attempted = false;
  bool recovery_required = false;
};
// DOS ILHOPFW -hf algorithm, with no interactive continuation on timeout.
// Caller owns the live card, saved full before capture, RAM bootstrap and
// recovery lifetime. Both buffers must be complete canonical 128 KiB images.
// DOS AMD, ST, SST and Atmel parallel-flash algorithms.
[[nodiscard]] Report program_parallel(Card &, p2_bus &,
                                      std::span<const uint8_t> before,
                                      std::span<const uint8_t> desired);
// DOS mode-2 firmware-mediated DOWNLOAD path. No host JEDEC or Genesis.
[[nodiscard]] Report program_firmware(Card &, p2_bus &,
                                      std::span<const uint8_t> before,
                                      std::span<const uint8_t> desired,
                                      uint32_t base_address = 0x7e0000);
} // namespace p2::flash
