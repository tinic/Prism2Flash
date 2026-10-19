/* SPDX-License-Identifier: GPL-2.0-only */
#include "flash_plan.h"
#include <algorithm>

namespace p2::flash {
CardResult<Plan> Plan::create(std::span<const uint8_t> before,
                              const firmware::PreparedImage &prepared) {
  if (before.size() != Bytes || prepared.entry() || prepared.segments().empty())
    return std::unexpected{P2_E_ARGUMENT};
  Plan plan;
  plan.before_ = ByteBuffer::allocate(Bytes);
  plan.desired_ = ByteBuffer::allocate(Bytes);
  if (!plan.before_ || !plan.desired_)
    return std::unexpected{P2_E_IO};
  std::copy(before.begin(), before.end(), plan.before_.bytes().begin());
  std::copy(before.begin(), before.end(), plan.desired_.bytes().begin());
  plan.base_address_ = prepared.segments()[0].address & ~uint32_t(Bytes - 1);
  if (plan.base_address_ != 0x7e0000 && plan.base_address_ != 0x3e0000)
    return std::unexpected{P2_E_ARGUMENT};
  for (const auto &segment : prepared.segments()) {
    if (segment.address < plan.base_address_ ||
        segment.address >= plan.base_address_ + Bytes ||
        segment.data.size() > plan.base_address_ + Bytes - segment.address)
      return std::unexpected{P2_E_ARGUMENT};
    const auto offset = segment.address - plan.base_address_;
    std::copy(segment.data.bytes().begin(), segment.data.bytes().end(),
              plan.desired_.bytes().begin() + offset);
  }
  // The STA descriptor spans a hole containing CIS and PDA. Descriptor
  // lengths never generate bytes: overlay only actual prepared segments.
  if (!std::equal(before.begin() + 0xfe00, before.begin() + 0x10800,
                  plan.desired_.bytes().begin() + 0xfe00))
    return std::unexpected{P2_E_VERIFY};
  return plan;
}
} // namespace p2::flash
