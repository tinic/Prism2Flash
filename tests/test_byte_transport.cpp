/* SPDX-License-Identifier: GPL-2.0-only */
#include "gayle_io.h"
#include "mock.h"
#include <cassert>

namespace {
struct ByteModel {
  p2_mock mock{};
  unsigned reads = 0, writes = 0;
  uint32_t address = 0;
  uint16_t port = 0;
  uint8_t value = 0;
  ByteModel() {
    assert(!p2_mock_init(&mock, 16));
    // The transport callbacks use a separate context only through the
    // standard-layout first member. All other bus callbacks still see mock.
    mock.bus.read8 = [](void *ctx, uint16_t reg) {
      auto &m = *static_cast<ByteModel *>(ctx);
      ++m.reads;
      m.port = reg;
      m.address = m.mock.aux_address;
      return m.value;
    };
    mock.bus.write8 = [](void *ctx, uint16_t reg, uint8_t value) {
      auto &m = *static_cast<ByteModel *>(ctx);
      ++m.writes;
      m.port = reg;
      m.address = m.mock.aux_address;
      m.value = value;
    };
    mock.bus.ctx = this;
  }
  ~ByteModel() { p2_mock_free(&mock); }
};
} // namespace
int main() {
  static_assert(p2::gayle_byte_offset(0x3e) == 0x3e);
  static_assert(p2::gayle_byte_offset(0x3f) == 0x1003e);
  static_assert(p2::gayle_byte_offset(0xffff) == 0x1fffe);
  for (uint32_t address :
       {0u, 1u, 0x7eu, 0x7fu, 0x80u, 0x7e5555u, 0x7e2aaau, 0x7fffffu}) {
    ByteModel m;
    p2::Card card{m.mock.bus};
    assert(card.write_nv_byte(address, 0xa5));
    assert(m.writes == 1 && !m.reads && m.value == 0xa5);
    assert(m.address == address && m.mock.aux_space == 1);
    assert(m.port == P2_AUXDATA + (address & 1));
    auto read = card.read_nv_byte(address);
    assert(read && *read == 0xa5 && m.reads == 1);
    assert(!m.mock.payload_writes && !m.mock.command_writes);
    assert(!m.mock.configuration_writes);
  }
  ByteModel m;
  p2::Card card{m.mock.bus};
  assert(card.write_nv_byte(0x800000, 0).error() == P2_E_ARGUMENT);
  assert(!m.mock.operations && !m.writes);
  m.mock.bus.write8 = nullptr;
  assert(card.read_nv_byte(0).error() == P2_E_UNSUPPORTED);
  assert(!m.mock.operations);
  m.mock.bus.write8 = [](void *, uint16_t, uint8_t) {};
  m.mock.present = 0;
  assert(card.read_nv_byte(0).error() == P2_E_REMOVED);
  m.mock.present = 1;
  m.mock.cancel = 1;
  assert(card.write_nv_byte(0, 0).error() == P2_E_CANCELLED);
  assert(!m.writes);
}
