/* SPDX-License-Identifier: GPL-2.0-only */
#include "card_profile.h"
namespace p2::profile {
CardResult<Observation> observe(const p2_card_info &cis,
                                std::span<const uint8_t, P2_PDA_SIZE> pda) {
  if (cis.manufacturer != 0x000b || cis.product != 0x7300)
    return std::unexpected{P2_E_UNSUPPORTED};
  if (p2_validate_pda(pda.data()))
    return std::unexpected{P2_E_PDA};
  auto word = [&](size_t offset) {
    return uint16_t(pda[offset] | uint16_t(pda[offset + 1]) << 8);
  };
  std::array<uint16_t, P2_PDA_SIZE / 4> seen{};
  size_t count = 0;
  bool nic = false, config = false;
  for (size_t pos = 0; pos + 4 <= pda.size();) {
    const size_t words = word(pos);
    const auto kind = word(pos + 2);
    if (!words || words * 2 > 512 || words + 1 > (pda.size() - pos) / 2)
      return std::unexpected{P2_E_PDA};
    if (!kind)
      break;
    for (size_t i = 0; i < count; ++i)
      if (seen[i] == kind)
        return std::unexpected{P2_E_PDA};
    if (count == seen.size())
      return std::unexpected{P2_E_PDA};
    seen[count++] = kind;
    if (kind == 8) {
      nic = words == 5 && word(pos + 4) == 0x800c && word(pos + 6) == 0 &&
            word(pos + 8) == 1 && word(pos + 10) == 0;
    }
    if (kind == 0x400)
      config = words == 2 && word(pos + 4) == 3;
    if (kind == 5)
      return std::unexpected{P2_E_UNSUPPORTED};
    pos += (words + 1) * 2;
  }
  if (!nic || !config)
    return std::unexpected{P2_E_UNSUPPORTED};
  Observation result;
  result.sram_width_inference = 16;
  return result;
}
bool programming_approved() { return false; }
} // namespace p2::profile
