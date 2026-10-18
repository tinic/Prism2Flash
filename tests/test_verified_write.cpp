/* SPDX-License-Identifier: GPL-2.0-only */
#include "mock.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace {
class Model {
public:
  Model() { assert(p2_mock_init(&state, 8) == P2_OK); }
  ~Model() { p2_mock_free(&state); }
  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;
  p2_mock state{};
};
void cross_page_and_transient_read() {
  Model model;
  auto &m = model.state;
  p2::Card card{m.bus};
  std::array<uint8_t, 260> bytes;
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<uint8_t>(i * 13 + 7);
  m.corrupt_ram_once_at = 1;
  const auto result = card.write_verified(0x7e007e, bytes);
  assert(!result.error && result.bytes_written == bytes.size());
  assert(result.bytes_verified == bytes.size() && result.disagreements == 1);
  assert(result.verification_reads ==
         9); // Four chunks, first needs three reads.
  assert(m.payload_writes ==
         bytes.size() / 2); // Never rewrite on bad readback.
  assert(std::equal(bytes.begin(), bytes.end(), m.memory + 0x7e007e));
  assert(!m.flash_commands && !m.configuration_writes && !m.command_writes);
}
void persistent_failure_and_deadline() {
  const std::array<uint8_t, 128> bytes{0x12, 0x34, 0x56, 0x78};
  {
    Model model;
    auto &m = model.state;
    m.drop_ram_writes = 1;
    const auto result = p2::Card{m.bus}.write_verified(0x7e0000, bytes);
    assert(result.error == P2_E_VERIFY && result.bytes_written == 128);
    assert(!result.bytes_verified && result.verification_reads == 20);
    assert(m.payload_writes == 64 && !m.flash_commands);
  }
  {
    Model model;
    auto &m = model.state;
    m.unstable_ram = 1;
    const auto result = p2::Card{m.bus}.write_verified(0x7e0000, bytes);
    assert(result.error == P2_E_VERIFY && result.verification_reads == 20);
    assert(result.disagreements == 19 && !result.bytes_verified);
    assert(m.payload_writes == 64);
  }
  for (bool wrap : {false, true}) {
    Model model;
    model.state.access_delay_us = 1000000;
    if (wrap)
      model.state.clock = UINT32_MAX - 10;
    const auto result =
        p2::Card{model.state.bus}.write_verified(0x7e0000, bytes);
    assert(result.error == P2_E_TIMEOUT && !result.bytes_verified);
    assert(model.state.payload_writes > 0 && model.state.payload_writes < 64);
    assert(result.elapsed_us < 31000000 && !result.bytes_written);
  }
}
void bounds_and_faults() {
  const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
  {
    Model model;
    p2::Card card{model.state.bus};
    assert(card.write_verified(1, bytes).error == P2_E_ARGUMENT);
    assert(card.write_verified(0x800000, bytes).error == P2_E_ARGUMENT);
    assert(card.write_verified(0x7ffffe, bytes).error == P2_E_ARGUMENT);
    assert(card.write_verified(0, std::span{bytes}.first(3)).error ==
           P2_E_ARGUMENT);
    assert(card.write_verified(0, {}).error == P2_E_ARGUMENT);
    assert(!model.state.operations);
  }
  unsigned boundaries;
  {
    Model model;
    assert(!p2::Card{model.state.bus}.write_verified(0x7e0038, bytes).error);
    boundaries = model.state.operations;
  }
  for (unsigned at = 1; at <= boundaries; ++at) {
    for (bool removal : {false, true}) {
      Model model;
      auto &m = model.state;
      if (removal)
        m.remove_at = at;
      else
        m.cancel_at = at;
      const auto result = p2::Card{m.bus}.write_verified(0x7e0038, bytes);
      assert(result.error == (removal ? P2_E_REMOVED : P2_E_CANCELLED));
      assert(result.bytes_verified <= result.bytes_written);
      assert(m.payload_writes <= 2 && !m.flash_commands &&
             !m.configuration_writes);
    }
  }
}
void genesis_aux_state() {
  for (bool enabled : {false, true}) {
    Model model;
    auto &m = model.state;
    m.regs[P2_CONTROL / 2] = enabled ? 0xc000 : 0;
    m.regs[P2_PARAM0 / 2] = 0x1234;
    m.regs[P2_AUXPAGE / 2] = 0xffff; // Opaque readback, never restore it.
    // First priming read consumes two words; first verification has one bad
    // word.
    m.corrupt_ram_once_at = 3;
    const auto result = p2::Card{m.bus}.probe(8);
    assert(!result.error && !result.cleanup_error && !result.aux_cleanup_error);
    assert(result.aux_was_enabled == enabled && result.reset_started);
    assert(result.verification_reads == 3 && result.disagreements == 1);
    assert(m.normal_returns == 1 && m.genesis_entries == 1);
    assert(m.regs[P2_CONTROL / 2] == (enabled ? 0xc000 : 0));
    assert(m.regs[P2_AUXOFFSET / 2] == 0 && !m.scratch_writes_after_disable);
    assert(m.regs[P2_PARAM0 / 2] == (enabled ? 0x1234 : 0));
    assert(m.payload_writes == 4 && !m.flash_commands);
  }
  {
    Model model;
    model.state.stuck_aux = 1;
    const auto result = p2::Card{model.state.bus}.probe(8);
    assert(result.error == P2_E_TIMEOUT &&
           result.aux_cleanup_error == P2_E_TIMEOUT);
    assert(!result.reset_started && !model.state.configuration_writes);
  }
  for (uint16_t control : {0xffff, 0x8000, 0x4000}) {
    Model model;
    model.state.regs[P2_CONTROL / 2] = control;
    const auto result = p2::Card{model.state.bus}.probe(8);
    assert(result.error == P2_E_IO && !result.reset_started);
    assert(!model.state.configuration_writes && !model.state.payload_writes);
  }
  {
    Model model;
    model.state.cor |= 0x80;
    const auto result = p2::Card{model.state.bus}.probe(8);
    assert(result.error == P2_E_IO && !result.reset_started);
    assert(!model.state.configuration_writes && !model.state.payload_writes);
  }
}
void genesis_fault_boundaries() {
  for (bool enabled : {false, true}) {
    unsigned boundaries;
    {
      Model model;
      model.state.regs[P2_CONTROL / 2] = enabled ? 0xc000 : 0;
      assert(!p2::Card{model.state.bus}.probe(8).error);
      boundaries = model.state.operations;
    }
    for (unsigned at = 1; at <= boundaries; ++at) {
      for (bool removal : {false, true}) {
        Model model;
        auto &m = model.state;
        m.regs[P2_CONTROL / 2] = enabled ? 0xc000 : 0;
        if (removal)
          m.remove_at = at;
        else
          m.cancel_at = at;
        const auto report = p2::Card{m.bus}.probe(8);
        assert(report.error == (removal ? P2_E_REMOVED : P2_E_CANCELLED));
        assert(m.payload_writes <= 4 && !m.flash_commands);
        if (!removal) {
          assert(!report.cleanup_error && !report.aux_cleanup_error);
          assert(!(m.cor & 0x80));
          assert(m.regs[P2_CONTROL / 2] == (enabled ? 0xc000 : 0));
          assert(m.normal_returns == (report.reset_started ? 1u : 0u));
          assert(m.bus.cancelled && m.bus.cancelled(m.bus.ctx));
        }
      }
    }
  }
  for (bool bad_write : {false, true}) {
    Model model;
    model.state.stick_aux_on_normal = 1;
    model.state.drop_ram_writes = bad_write;
    const auto report = p2::Card{model.state.bus}.probe(8);
    assert(report.error == (bad_write ? P2_E_VERIFY : P2_E_TIMEOUT));
    assert(report.cleanup_error == P2_E_TIMEOUT &&
           report.aux_cleanup_error == P2_E_TIMEOUT);
    assert(model.state.normal_returns == 1 && model.state.payload_writes == 4);
    assert(!model.state.flash_commands);
  }
}
} // namespace
int main() {
  cross_page_and_transient_read();
  persistent_failure_and_deadline();
  bounds_and_faults();
  genesis_aux_state();
  genesis_fault_boundaries();
}
