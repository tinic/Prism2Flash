/* SPDX-License-Identifier: GPL-2.0-only
 * Byte-oriented SHA-256, implemented from the FIPS 180-4 algorithm.
 */
#include "sha256.h"
#include <algorithm>
#include <bit>
namespace p2 {
void Sha256::transform() {
  static constexpr std::array<uint32_t, 64> k = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<uint32_t, 64> w;
  auto [a, b, c, d, e, f, g, h] = h_;
  uint32_t t1, t2;
  unsigned i;
  for (i = 0; i < 16; ++i)
    w[i] = ((uint32_t)block_[i * 4] << 24) |
           ((uint32_t)block_[i * 4 + 1] << 16) |
           ((uint32_t)block_[i * 4 + 2] << 8) | block_[i * 4 + 3];
  for (i = 16; i < 64; ++i)
    w[i] =
        w[i - 16] +
        (std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^
         (w[i - 15] >> 3)) +
        w[i - 7] +
        (std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
  for (i = 0; i < 64; ++i) {
    t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
         ((e & f) ^ (~e & g)) + k[i] + w[i];
    t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
         ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}
void Sha256::update(std::span<const uint8_t> data) {
  bytes_ += data.size();
  while (!data.empty()) {
    const auto count = std::min(block_.size() - used_, data.size());
    std::copy_n(data.begin(), count, block_.begin() + used_);
    used_ += count;
    data = data.subspan(count);
    if (used_ == block_.size()) {
      transform();
      used_ = 0;
    }
  }
}
std::array<char, 65> Sha256::hex() const {
  auto copy = *this;
  return copy.finish();
}
std::array<char, 65> Sha256::finish() {
  std::array<char, 65> out{};
  static const char hex[] = "0123456789abcdef";
  uint64_t bits = bytes_ * 8;
  unsigned i;
  block_[used_++] = 0x80;
  if (used_ > 56) {
    std::fill(block_.begin() + used_, block_.end(), 0);
    transform();
    used_ = 0;
  }
  std::fill(block_.begin() + used_, block_.begin() + 56, 0);
  for (i = 0; i < 8; ++i)
    block_[63 - i] = (uint8_t)(bits >> (i * 8));
  transform();
  for (i = 0; i < 32; ++i) {
    uint8_t b = (uint8_t)(h_[i / 4] >> (24 - (i % 4) * 8));
    out[i * 2] = hex[b >> 4];
    out[i * 2 + 1] = hex[b & 15];
  }
  return out;
}

} // namespace p2
