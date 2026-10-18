/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <cstdint>

namespace p2 {
// Add one tick for sample phase: a single E-clock transition can occur less
// than one microsecond after sampling. No CPU-frequency calibration is used.
constexpr uint32_t nv_gap_ticks(uint32_t frequency) {
  return uint32_t((uint64_t(frequency) + 999999) / 1000000) + 1;
}
template <typename ReadClock> bool wait_nv_gap(uint32_t ticks, ReadClock read) {
  const auto start = read();
  // This is a fault bound, not a CPU-counted delay. Success always requires
  // measured time to advance; a broken clock must not trap card ownership.
  for (unsigned samples = 0; samples < 1024; ++samples)
    if (uint32_t(read() - start) >= ticks)
      return true;
  return false;
}
} // namespace p2
