/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <cstdint>

namespace p2 {
// Linux v6.17 arch/m68k/include/asm/amigayle.h: GAYLE_ODD=0xffff.
// Odd byte ports occupy the second bank at even CPU addresses. Word ports
// remain in the first bank; applying this mapping to words is incorrect.
constexpr uint32_t gayle_byte_offset(uint16_t port) {
  return port + ((port & 1u) ? 0xffffu : 0u);
}
} // namespace p2
