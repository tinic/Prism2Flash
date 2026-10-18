/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "platform.h"
namespace p2::profile {
inline constexpr auto CandidateId = "ma401-pd5lmwp200-candidate-1";
struct Observation {
  unsigned sram_width_inference = 0;
  bool ram_size_present = false;
  // These cannot be established by a PDA or a matching CIS/NIC tuple.
  bool physical_coverage_verified = false;
  bool sram_capacity_verified = false;
  bool hardware_authorization = false;
};
// Corroborate a saved PDA without INIT or reading a card. Exact tuple required;
// this reports observations and cannot issue an operation capability.
[[nodiscard]] CardResult<Observation>
observe(const p2_card_info &cis, std::span<const uint8_t, P2_PDA_SIZE> pda);
// No programming profile exists. RAM-only admission uses the user-accepted
// full 8 MiB NV map and fresh owned capture, never a saved observation flag.
[[nodiscard]] bool programming_approved();
} // namespace p2::profile
