/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "firmware.h"
namespace p2::firmware {
struct Selection {
  std::optional<Image> primary;
  Image station;
  std::size_t primary_index, station_index;
};
// Select by validated component/platform/interface metadata, never card
// branding.
[[nodiscard]] Result<Selection> select(std::span<const std::string_view> paths,
                                       const CardSnapshot &,
                                       std::span<const uint8_t> pda);
} // namespace p2::firmware
