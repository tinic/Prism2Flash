/* SPDX-License-Identifier: GPL-2.0-only */
#include "host_flash.h"
#include "mock.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>

namespace {
struct FlashModel {
  p2_mock mock{};
  std::vector<uint8_t> before, desired;
  unsigned stage = 0, erases = 0, programs = 0, commands = 0;
  bool id_mode = false, unknown = false, erase_stuck = false;
  bool program_stuck = false, cancel_after_erase = false;
  bool cancel_in_id = false, remove_in_id = false,
       cancel_on_erase_write = false;
  uint32_t unlock_address = 0;
  FlashModel() : before(p2::flash::Bytes, 0xff), desired(before) {
    assert(!p2_mock_init(&mock, 16));
    mock.nv_memory = static_cast<uint8_t *>(std::malloc(0x800000));
    assert(mock.nv_memory);
    before[0] = 0x12;
    before[0xfe00] = 0x91; // CIS and PDA must survive the whole-chip erase.
    before[0x10020] = 0x31;
    desired = before;
    desired[0x1234] = 0xa5;
    for (uint32_t base = 0; base < 0x800000; base += p2::flash::Bytes)
      std::copy(before.begin(), before.end(), mock.nv_memory + base);
    mock.bus.ctx = this;
    mock.bus.read8 = [](void *ctx, uint16_t port) {
      auto &m = *static_cast<FlashModel *>(ctx);
      const auto offset = (m.mock.aux_address & 0x1ffff) + (port & 1);
      assert(m.mock.aux_space == 1 && port == P2_AUXDATA + (offset & 1));
      if (m.cancel_after_erase && m.erases)
        m.mock.cancel = 1;
      if (m.id_mode) {
        if (m.cancel_in_id)
          m.mock.cancel = 1;
        if (m.remove_in_id)
          m.mock.present = 0;
        return static_cast<uint8_t>(m.unknown || m.unlock_address != 0x5555
                                        ? 0xff
                                    : offset == 0 ? 0xbf
                                                  : 0xd5);
      }
      return m.mock.nv_memory[offset];
    };
    mock.bus.write8 = [](void *ctx, uint16_t port, uint8_t value) {
      auto &m = *static_cast<FlashModel *>(ctx);
      const auto address = (m.mock.aux_address & 0x1ffff) + (port & 1);
      assert(m.mock.aux_space == 1 && port == P2_AUXDATA + (address & 1));
      ++m.commands;
      if (m.stage == 3) {
        ++m.programs;
        if (!m.program_stuck)
          for (uint32_t base = 0; base < 0x800000; base += p2::flash::Bytes)
            m.mock.nv_memory[base + address] &= value;
        m.stage = 0;
      } else if (value == 0xf0) {
        m.id_mode = false;
        m.stage = 0;
      } else if (m.stage == 0 || m.stage == 4) {
        assert(value == 0xaa && (address == 0x555 || address == 0x5555));
        m.unlock_address = address;
        m.stage = m.stage == 4 ? 5 : 1;
      } else if (m.stage == 1 || m.stage == 5) {
        assert(value == 0x55 &&
               address == (m.unlock_address == 0x5555 ? 0x2aaa : 0x2aa));
        m.stage = m.stage == 5 ? 6 : 2;
      } else if (m.stage == 2) {
        assert(address == m.unlock_address);
        if (value == 0x90) {
          m.id_mode = true;
          m.stage = 0;
        } else if (value == 0xa0)
          m.stage = 3;
        else {
          assert(value == 0x80);
          m.stage = 4;
        }
      } else {
        assert(m.stage == 6 && value == 0x10 && address == 0x5555);
        ++m.erases;
        if (m.cancel_on_erase_write)
          m.mock.cancel = 1;
        if (!m.erase_stuck)
          std::fill_n(m.mock.nv_memory, 0x800000, 0xff);
        m.stage = 0;
      }
    };
  }
  ~FlashModel() { p2_mock_free(&mock); }
  auto run() {
    p2::Card card{mock.bus};
    const auto result = p2::flash::program_sst(card, mock.bus, before, desired);
    // NvCapture reads CONTROL first; a completed word verification must not
    // leave the NV selector obscuring normal registers on the Amiga.
    if (mock.present)
      assert(mock.aux_space == 0);
    return result;
  }
};
} // namespace
int main() {
  {
    FlashModel m;
    m.cancel_in_id = true;
    p2::Card card{m.mock.bus};
    const auto result = p2::flash::identify(card);
    assert(!result && result.error() == P2_E_CANCELLED);
    assert(!m.id_mode && !m.stage && !m.erases && !m.programs);
    assert(m.mock.cancel && !m.mock.aux_space);
  }
  {
    FlashModel m;
    m.remove_in_id = true;
    p2::Card card{m.mock.bus};
    const auto result = p2::flash::identify(card);
    assert(!result && result.error() == P2_E_REMOVED);
    assert(m.commands == 4 && !m.erases && !m.programs);
  }
  {
    FlashModel m;
    m.cancel_on_erase_write = true;
    const auto result = m.run();
    assert(result.error == P2_E_CANCELLED && result.erase_command_attempted);
    assert(!result.erase_started && result.recovery_required && m.erases == 1);
  }
  {
    FlashModel m;
    const auto r = m.run();
    assert(!r.error && r.erase_started && !r.recovery_required);
    assert(r.manufacturer == 0xbf && r.device == 0xd5);
    assert(r.verified_bytes == p2::flash::Bytes && r.programmed_bytes == 4);
    assert(m.erases == 1 && m.programs == 4);
    assert(std::equal(m.desired.begin(), m.desired.end(), m.mock.nv_memory));
    assert(!m.mock.flash_commands && !m.mock.configuration_writes);
  }
  {
    FlashModel m;
    m.desired = m.before;
    const auto r = m.run();
    assert(!r.error && !r.erase_started && !m.erases && !m.programs);
  }
  for (bool pda : {false, true}) {
    FlashModel m;
    m.desired[pda ? 0x10000 : 0xfe00] ^= 1;
    assert(m.run().error == P2_E_VERIFY);
    assert(!m.commands && !m.erases);
  }
  {
    FlashModel m;
    m.unknown = true;
    assert(m.run().error == P2_E_UNSUPPORTED);
    assert(!m.erases && !m.programs);
  }
  {
    FlashModel m;
    m.mock.nv_memory[0x380100] ^= 1;
    assert(m.run().error == P2_E_VERIFY);
    assert(!m.erases && !m.programs);
  }
  for (bool wrap : {false, true}) {
    FlashModel m;
    m.erase_stuck = true;
    if (wrap)
      m.mock.clock = UINT32_MAX - 10000;
    const auto r = m.run();
    assert(r.error == P2_E_TIMEOUT && r.erase_started && r.recovery_required);
    assert(m.erases == 1 && !m.programs);
  }
  {
    FlashModel m;
    m.program_stuck = true;
    const auto r = m.run();
    assert(r.error == P2_E_TIMEOUT && r.recovery_required && m.programs == 1);
  }
  {
    FlashModel m;
    m.cancel_after_erase = true;
    const auto r = m.run();
    assert(r.error == P2_E_CANCELLED && r.recovery_required);
    assert(m.erases == 1 && !m.programs);
  }
  {
    FlashModel m;
    m.mock.corrupt_nv_after = p2::flash::Bytes / 2;
    const auto r = m.run();
    assert(r.error == P2_E_VERIFY && r.recovery_required && !r.verified_bytes);
    assert(m.erases == 1 && m.programs == 4);
  }
  {
    FlashModel m;
    m.mock.present = 0;
    assert(m.run().error == P2_E_REMOVED && !m.commands);
  }
}
