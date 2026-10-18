/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "firmware.h"

namespace p2::bootstrap {
struct Report {
  p2_error error = P2_OK;
  ProbeReport genesis{};
  uint32_t last_address = 0;
  std::size_t bytes_written = 0, bytes_verified = 0, bytes_skipped = 0;
  bool recovery_required = false, initial_started = false;
};
// Original DOS mode0: Genesis, raw EXTDS load/verify, then low07 handoff.
// No INIT, DOWNLOAD or flash command. A load failure retains Genesis and RAM.
[[nodiscard]] Report load(Card &, p2_bus &, const firmware::InitialImage &);
} // namespace p2::bootstrap
