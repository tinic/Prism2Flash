/* SPDX-License-Identifier: GPL-2.0-only */
#include "bootstrap.h"
#include "mock.h"
#include <cassert>
#include <cstring>
#include <string_view>

namespace {
constexpr std::string_view Source =
    "S31500000840000102030405060708090A0B0C0D0E0F2A\n"
    "S309007FE000414243448D\nS70500000000FA\n";
struct Model {
  p2_mock mock{};
  Model() { assert(!p2_mock_init(&mock, 16)); }
  ~Model() { p2_mock_free(&mock); }
};
auto initial() {
  auto image = p2::firmware::InitialImage::parse(
      {reinterpret_cast<const uint8_t *>(Source.data()), Source.size()});
  assert(image);
  return std::move(*image);
}
} // namespace
int main() {
  const auto image = initial();
  for (bool skip : {false, true}) {
    Model m;
    if (skip)
      for (unsigned i = 0; i < 16; ++i)
        m.mock.memory[0x840 + i] = i;
    p2::Card card{m.mock.bus};
    const auto r = p2::bootstrap::load(card, m.mock.bus, image);
    assert(!r.error && r.initial_started && !r.recovery_required);
    assert(r.bytes_written == (skip ? 4u : 20u));
    assert(r.bytes_verified == r.bytes_written);
    assert(r.bytes_skipped == (skip ? 16u : 0u));
    assert(!std::memcmp(m.mock.memory + 0x7fe000, "ABCD", 4));
    assert(m.mock.genesis_entries == 2 && m.mock.normal_returns == 1);
    assert(!m.mock.command_writes && !m.mock.flash_commands);
  }
  {
    Model m;
    // Four sentinel word writes complete before initial loading begins.
    m.mock.drop_ram_after = 4;
    p2::Card card{m.mock.bus};
    const auto r = p2::bootstrap::load(card, m.mock.bus, image);
    assert(r.error == P2_E_VERIFY && r.recovery_required && !r.initial_started);
    assert(r.genesis.genesis_latches == 2 && !m.mock.normal_returns);
    assert(m.mock.ccsr == 0x0f && !m.mock.command_writes);
  }
  {
    Model m;
    m.mock.present = 0;
    p2::Card card{m.mock.bus};
    const auto r = p2::bootstrap::load(card, m.mock.bus, image);
    assert(r.error == P2_E_REMOVED && !r.recovery_required);
    assert(!m.mock.configuration_writes && !m.mock.payload_writes);
  }
}
