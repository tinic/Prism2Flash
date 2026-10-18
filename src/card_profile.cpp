/* SPDX-License-Identifier: GPL-2.0-only */
#include "card_profile.h"
namespace p2::profile {
CardResult<Observation> observe(const p2_card_info &cis,
                                std::span<const uint8_t, P2_PDA_SIZE> pda) {
  (void)cis; // Branding is checked at acquisition; PDA defines the hardware.
  if (p2_validate_pda(pda.data()))
    return std::unexpected{P2_E_PDA};
  auto word = [&](size_t offset) {
    return uint16_t(pda[offset] | uint16_t(pda[offset + 1]) << 8);
  };
  std::array<uint16_t, P2_PDA_SIZE / 4> seen{};
  size_t count = 0;
  bool nic = false;
  Observation result;
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
      nic = words == 5 && Card::supports(word(pos + 4));
      if (nic)
        result.nic = {word(pos + 4), word(pos + 6), word(pos + 8),
                      word(pos + 10)};
    }
    if (kind == 0x400) {
      if (words != 2 || (word(pos + 4) & 4))
        return std::unexpected{
            P2_E_UNSUPPORTED}; // PCI configuration on PC Card.
      result.sram_width_inference = (word(pos + 4) & 2) ? 16 : 8;
    }
    if (kind == 5)
      result.ram_size_present = true;
    pos += (words + 1) * 2;
  }
  if (!nic)
    return std::unexpected{P2_E_UNSUPPORTED};
  return result;
}
} // namespace p2::profile
