/* SPDX-License-Identifier: GPL-2.0-only */
#include "host_flash.h"
#include "mock.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>
namespace {
struct Model {
  p2_mock mock{};
  p2_bus original{};
  std::vector<uint8_t> before, desired;
  uint16_t page = 0x60, offset = 0, size = 4096, maximum = 5000;
  unsigned enables = 0, commits = 0, finishes = 0, fail_commit = 0;
  uint32_t destination = 0;
  uint16_t count = 0;
  bool bad_record = false, bad_program = false, drop_commit_event = false,
       cancel_after_commit = false;
  Model() : before(p2::flash::Bytes), desired(p2::flash::Bytes) {
    assert(!p2_mock_init(&mock, 16));
    original = mock.bus;
    mock.nv_memory = static_cast<uint8_t *>(std::malloc(0x800000));
    assert(mock.nv_memory);
    for (size_t i = 0; i < before.size(); ++i)
      before[i] = uint8_t(i * 31 + i / 256);
    desired = before;
    desired[0x1234] ^= 0x55;
    desired[0x1f102] ^= 0xaa;
    for (size_t b = 0; b < 0x800000; b += before.size())
      std::copy(before.begin(), before.end(), mock.nv_memory + b);
    mock.bus.ctx = this;
    mock.bus.read16 = [](void *ctx, uint16_t reg) -> uint16_t {
      auto &m = *static_cast<Model *>(ctx);
      if (reg == P2_DATA0) {
        const auto rid = m.mock.regs[P2_SELECT0 / 2];
        if (rid == 0xfd01 || rid == 0xfd00) {
          const auto i = m.mock.data_index++;
          if (!i)
            return m.bad_record ? 9 : rid == 0xfd01 ? 4 : 2;
          if (i == 1)
            return rid;
          if (rid == 0xfd00)
            return m.maximum;
          return i == 2 ? m.page : i == 3 ? m.offset : m.size;
        }
      }
      return m.original.read16(&m.mock, reg);
    };
    mock.bus.write16 = [](void *ctx, uint16_t reg, uint16_t value) {
      auto &m = *static_cast<Model *>(ctx);
      if (reg == P2_CMD && value == 0x222) {
        m.destination = m.mock.regs[P2_PARAM0 / 2] |
                        (uint32_t(m.mock.regs[P2_PARAM1 / 2]) << 16);
        m.count = m.mock.regs[P2_PARAM2 / 2];
        ++m.enables;
        assert(m.count && m.count <= m.size && !(m.count & 1));
      }
      m.original.write16(&m.mock, reg, value);
      if (reg == P2_CMD && value == 0x322) {
        ++m.commits;
        if (m.fail_commit == m.commits)
          m.mock.regs[P2_STATUS / 2] = 0x0100;
        else {
          const auto *ram = m.mock.memory + (uint32_t(m.page) << 7) + m.offset;
          for (size_t b = 0; b < 0x800000; b += p2::flash::Bytes)
            std::copy_n(ram, m.count,
                        m.mock.nv_memory + b + (m.destination & 0x1ffff));
          if (m.bad_program)
            m.mock.nv_memory[m.destination] ^= 1;
        }
        if (m.drop_commit_event)
          m.mock.regs[P2_EVSTAT / 2] = 0;
        if (m.cancel_after_commit)
          m.mock.cancel = 1;
      }
      if (reg == P2_CMD && value == 0x22) {
        ++m.finishes;
        m.mock.regs[P2_CONTROL / 2] = 0;
      }
    };
  }
  ~Model() { p2_mock_free(&mock); }
  auto run(uint32_t base = 0x7e0000) {
    p2::Card card{mock.bus};
    return p2::flash::program_firmware(card, mock.bus, before, desired, base);
  }
};
} // namespace
int main() {
  for (auto base : {0x7e0000u, 0x3e0000u})
    for (uint16_t size : {uint16_t(4096), uint16_t(1024)}) {
      Model m;
      m.size = size;
      const auto r = m.run(base);
      assert(!r.error && !r.recovery_required && r.program_command_attempted);
      assert(r.verified_bytes == p2::flash::Bytes &&
             m.commits == p2::flash::Bytes / size && m.finishes == 1);
      assert(!m.mock.configuration_writes && !m.mock.genesis_entries);
      assert(std::equal(m.desired.begin(), m.desired.end(), m.mock.nv_memory));
    }
  {
    Model m;
    m.desired = m.before;
    auto r = m.run();
    assert(!r.error && !m.enables && !m.commits && !m.finishes);
  }
  {
    Model m;
    m.desired[0x10000] ^= 1;
    assert(m.run().error == P2_E_VERIFY);
    assert(!m.mock.command_writes);
  }
  {
    Model m;
    m.offset = 128;
    assert(m.run().error == P2_E_UNSUPPORTED);
    assert(!m.enables);
  }
  {
    Model m;
    m.size = 3;
    assert(m.run().error == P2_E_UNSUPPORTED);
    assert(!m.enables);
  }
  for (uint16_t maximum : {uint16_t(0), uint16_t(65535)}) {
    Model m;
    m.maximum = maximum;
    assert(!m.run().error && m.finishes == 1);
  }
  {
    Model m;
    m.bad_record = true;
    assert(m.run().error == P2_E_IO);
    assert(!m.enables);
  }
  {
    Model m;
    m.mock.nv_memory[0x7e0020] ^= 1;
    assert(m.run().error == P2_E_VERIFY);
    assert(!m.enables);
  }
  {
    Model m;
    m.fail_commit = 2;
    auto r = m.run();
    assert(r.error == P2_E_COMMAND && r.recovery_required && m.commits == 2 &&
           !m.finishes);
  }
  {
    Model m;
    m.bad_program = true;
    auto r = m.run();
    assert(r.error == P2_E_VERIFY && r.recovery_required && m.commits == 1 &&
           !m.finishes);
  }
  {
    Model m;
    m.drop_commit_event = true;
    auto r = m.run();
    assert(r.error == P2_E_TIMEOUT && r.recovery_required && m.commits == 1 &&
           !m.finishes);
  }
  {
    Model m;
    m.cancel_after_commit = true;
    auto r = m.run();
    assert(r.error == P2_E_CANCELLED && r.recovery_required && m.commits == 1 &&
           !m.finishes);
  }
  {
    Model m;
    m.mock.drop_ram_writes = 1;
    auto r = m.run();
    assert(r.error == P2_E_VERIFY && r.recovery_required && !m.commits &&
           !m.finishes);
  }
  {
    Model m;
    m.mock.present = 0;
    assert(m.run().error == P2_E_REMOVED);
    assert(!m.commits);
  }
}
