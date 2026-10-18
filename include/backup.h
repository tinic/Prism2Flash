/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "prism2.h"
#include <stdio.h>
#include <string_view>
namespace p2::backup {
inline constexpr uint32_t NvBytes = 0x800000;
struct CaptureReport {
  p2_error error = P2_OK;
  std::array<uint32_t, 2> bytes{};
  uint32_t elapsed_us = 0;
  int cleanup_error = 0, backend_cleanup_error = 0;
  std::array<std::array<char, 65>, 2> sha256{};
  uint32_t mismatch_bytes = 0, first_mismatch = 0, comparison_read_bytes = 0;
  uint32_t page_reads = 0, page_disagreements = 0;
  std::array<uint16_t, 6> before{}, after{};
};
// A report retains partial acquisition and cleanup evidence even on failure.
[[nodiscard]] CaptureReport capture(p2_bus &bus, FILE &first, FILE &second);
[[nodiscard]] bool valid_utc(std::string_view timestamp);
int run_command(const char *directory, const char *utc);
// Exact length/hash check, including checked read-stream close. Also used for
// saved transaction manifests; defaults to the complete AUX NV capture size.
int verify_file(const char *name, const char *sha256, uint32_t size = NvBytes);
} // namespace p2::backup
