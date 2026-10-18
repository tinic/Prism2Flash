/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "platform.h"
namespace p2::profile {
struct Observation {
  Identity nic{};
  unsigned sram_width_inference = 0; // Zero: use Host AP runtime detection.
  bool ram_size_present = false;
};
// Validate the saved PDA and read this card's actual NIC/SRAM configuration.
[[nodiscard]] CardResult<Observation>
observe(const p2_card_info &cis, std::span<const uint8_t, P2_PDA_SIZE> pda);
} // namespace p2::profile
