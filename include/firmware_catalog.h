/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "firmware.h"
namespace p2::firmware {
struct Selection {
  Image primary, station;
  std::size_t primary_index, station_index;
};
// Select by validated component/platform/interface metadata, never card
// branding.
[[nodiscard]] Result<Selection> select(std::span<const std::string_view> paths,
                                       const CardSnapshot &,
                                       std::span<const uint8_t> pda);
// Original Intersil initial-image release codes for PC Card platforms.
[[nodiscard]] std::string_view initial_codes(uint16_t nic, unsigned bits);
} // namespace p2::firmware
