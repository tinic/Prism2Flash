/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "firmware.h"
#include "host_flash.h"

namespace p2::flash {
class Plan {
public:
  Plan(Plan &&) = default;
  Plan &operator=(Plan &&) = default;
  [[nodiscard]] static CardResult<Plan> create(std::span<const uint8_t> before,
                                               const firmware::PreparedImage &);
  [[nodiscard]] auto before() const { return before_.bytes(); }
  [[nodiscard]] auto desired() const { return desired_.bytes(); }

private:
  Plan() = default;
  ByteBuffer before_, desired_;
};
} // namespace p2::flash
