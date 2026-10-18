/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
namespace p2 {
class Sha256 {
public:
  void update(std::span<const uint8_t> bytes);
  void update(std::string_view text) {
    update({reinterpret_cast<const uint8_t *>(text.data()), text.size()});
  }
  // Taking a digest leaves the accumulated state usable for more input.
  [[nodiscard]] std::array<char, 65> hex() const;

private:
  void transform();
  std::array<char, 65> finish();
  std::array<uint32_t, 8> h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint64_t bytes_ = 0;
  std::size_t used_ = 0;
  std::array<uint8_t, 64> block_{};
};
} // namespace p2
