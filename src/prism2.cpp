/* SPDX-License-Identifier: GPL-2.0-only
 * Genesis/AUX sequence adapted from Linux v4.4 Host AP hostap_download.c
 * and hostap_cs.c, Copyright (c) 2001-2002 SSH Communications Security Corp
 * and Jouni Malinen; Copyright (c) 2002-2005 Jouni Malinen.
 * See docs/SOURCES.md for the pinned source files and provenance.
 */
#include "prism2.h"
#include <algorithm>
#include <cstring>

const char *p2_error_string(int rc) {
  static constexpr std::array names{"OK",
                                    "invalid argument",
                                    "card removed",
                                    "hardware timeout",
                                    "I/O failure",
                                    "unsupported card",
                                    "readback mismatch",
                                    "cancelled",
                                    "card command failed",
                                    "invalid PDA"};
  return rc >= 0 && static_cast<std::size_t>(rc) < names.size()
             ? names[rc]
             : "unknown error";
}
namespace p2 {
namespace {
// Only cancellation is masked for cleanup. Presence stays sticky and checked.
class IgnoreCancellation {
public:
  explicit IgnoreCancellation(p2_bus &bus) : bus_(bus), saved_(bus.cancelled) {
    bus_.cancelled = nullptr;
  }
  ~IgnoreCancellation() { bus_.cancelled = saved_; }
  IgnoreCancellation(const IgnoreCancellation &) = delete;
  IgnoreCancellation &operator=(const IgnoreCancellation &) = delete;

private:
  p2_bus &bus_;
  int (*saved_)(void *);
};
} // namespace
CardStatus Card::alive() const {
  if (!bus_.present(bus_.ctx))
    return std::unexpected{P2_E_REMOVED};
  return {};
}
CardStatus Card::interrupted() const {
  if (auto result = alive(); !result)
    return result;
  if (bus_.cancelled && bus_.cancelled(bus_.ctx))
    return std::unexpected{P2_E_CANCELLED};
  return {};
}
CardStatus Card::wait_mask(uint16_t reg, uint16_t mask, uint16_t expected,
                           uint32_t timeout) {
  const auto start = bus_.now_us(bus_.ctx);
  for (;;) {
    if (auto result = interrupted(); !result)
      return result;
    if ((bus_.read16(bus_.ctx, reg) & mask) == expected)
      return {};
    if (static_cast<uint32_t>(bus_.now_us(bus_.ctx) - start) >= timeout)
      return std::unexpected{P2_E_TIMEOUT};
    bus_.delay_us(bus_.ctx, 10);
  }
}
CardStatus Card::enable_aux(bool enable) {
  if (auto result = wait_mask(P2_CMD, 0x8000, 0, 5000); !result)
    return result;
  const auto control = bus_.read16(bus_.ctx, P2_CONTROL);
  bus_.write16(bus_.ctx, P2_PARAM0, enable ? 0xfe01 : 0);
  bus_.write16(bus_.ctx, P2_PARAM1, enable ? 0xdc23 : 0);
  bus_.write16(bus_.ctx, P2_PARAM2, enable ? 0xba45 : 0);
  bus_.write16(
      bus_.ctx, P2_CONTROL,
      static_cast<uint16_t>((control & 0x3fff) | (enable ? 0x8000 : 0x4000)));
  bus_.delay_us(bus_.ctx, 5);
  return wait_mask(P2_CONTROL, 0xc000, enable ? 0xc000 : 0, 100000);
}
CardStatus Card::transfer(uint32_t address, std::span<uint8_t> read,
                          std::span<const uint8_t> write, uint16_t space) {
  auto length = write.empty() ? read.size() : write.size();
  // AUXPAGE is 16 bits and its offset 7 bits. Never silently truncate.
  if ((address & 1) || (length & 1) || address >= 0x800000 ||
      length > 0x800000 - address)
    return std::unexpected{P2_E_ARGUMENT};
  while (length) {
    if (auto result = interrupted(); !result)
      return result;
    const auto count = std::min<std::size_t>(length, 128 - (address & 127));
    bus_.write16(bus_.ctx, P2_AUXPAGE, static_cast<uint16_t>(address >> 7));
    bus_.write16(bus_.ctx, P2_AUXOFFSET,
                 static_cast<uint16_t>((address & 127) | space));
    bus_.delay_us(bus_.ctx, 5);
    for (std::size_t i = 0; i < count; i += 2) {
      if (auto result = interrupted(); !result)
        return result;
      if (!write.empty()) {
        const auto value = static_cast<uint16_t>(
            write[i] | (static_cast<uint16_t>(write[i + 1]) << 8));
        bus_.write16(bus_.ctx, P2_AUXDATA, value);
      } else {
        const auto value = bus_.read16(bus_.ctx, P2_AUXDATA);
        read[i] = static_cast<uint8_t>(value);
        read[i + 1] = static_cast<uint8_t>(value >> 8);
      }
    }
    address += static_cast<uint32_t>(count);
    length -= count;
    if (!write.empty())
      write = write.subspan(count);
    else
      read = read.subspan(count);
  }
  return alive();
}
CardStatus Card::read_aux(uint32_t address, std::span<uint8_t> data) {
  return transfer(address, data, {}, 0);
}
CardStatus Card::read_nv(uint32_t address, std::span<uint8_t> data) {
  return transfer(address, data, {}, 0x1000);
}
CardStatus Card::write_aux(uint32_t address, std::span<const uint8_t> data) {
  return transfer(address, {}, data, 0);
}
CardStatus Card::command(uint16_t cmd, uint16_t param) {
  // Deliberately no DOWNLOAD/PROGRAM (0x22) path in this milestone.
  if (cmd != 0 && cmd != 0x21)
    return std::unexpected{P2_E_ARGUMENT};
  if (auto result = wait_mask(P2_CMD, 0x8000, 0, 5000); !result)
    return result;
  bus_.write16(bus_.ctx, P2_EVACK, P2_EV_CMD);
  bus_.write16(bus_.ctx, P2_PARAM0, param);
  bus_.write16(bus_.ctx, P2_CMD, cmd);
  if (auto result = wait_mask(P2_EVSTAT, P2_EV_CMD, P2_EV_CMD, 500000); !result)
    return result;
  const auto failed = bus_.read16(bus_.ctx, P2_STATUS) & 0x7f00;
  bus_.write16(bus_.ctx, P2_EVACK, P2_EV_CMD);
  if (failed)
    return std::unexpected{P2_E_COMMAND};
  return {};
}
CardStatus Card::initialize() {
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(bus_.ctx, P2_INTEN, 0);
  bus_.write16(bus_.ctx, P2_EVACK, 0xffff);
  return command(0, 0);
}
CardResult<Identity> Card::identity(uint16_t rid) {
  if (rid != P2_RID_NICID && rid != P2_RID_PRIID && rid != P2_RID_STAID)
    return std::unexpected{P2_E_ARGUMENT};
  if (auto result = command(0x21, rid); !result)
    return std::unexpected{result.error()};
  bus_.write16(bus_.ctx, P2_SELECT0, rid);
  bus_.write16(bus_.ctx, P2_OFFSET0, 0);
  if (auto result = wait_mask(P2_OFFSET0, 0x8000, 0, 50000); !result)
    return std::unexpected{result.error()};
  if (bus_.read16(bus_.ctx, P2_OFFSET0) & 0x4000)
    return std::unexpected{P2_E_IO};
  const auto words = bus_.read16(bus_.ctx, P2_DATA0);
  const auto type = bus_.read16(bus_.ctx, P2_DATA0);
  if (words != 5 || type != rid)
    return std::unexpected{P2_E_IO};
  if (auto result = interrupted(); !result)
    return std::unexpected{result.error()};
  const Identity id{
      bus_.read16(bus_.ctx, P2_DATA0), bus_.read16(bus_.ctx, P2_DATA0),
      bus_.read16(bus_.ctx, P2_DATA0), bus_.read16(bus_.ctx, P2_DATA0)};
  if (auto result = alive(); !result)
    return std::unexpected{result.error()};
  return id;
}
bool Card::supports(uint16_t id) {
  // HFA3842 release IDs. Early HFA3841, SSF and ambiguous 800c await profiles.
  constexpr std::array<uint16_t, 9> ids{0x800b, 0x800d, 0x8012, 0x8016, 0x801a,
                                        0x8017, 0x801b, 0x8018, 0x801c};
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}
CardResult<uint32_t> Card::read_pda(std::span<uint8_t, P2_PDA_SIZE> data) {
  constexpr std::array<uint32_t, 4> addresses{0x7f0000, 0x3f0000, 0x390000,
                                              0x7f0002};
  if (auto result = enable_aux(true); !result)
    return std::unexpected{result.error()};
  CardResult<uint32_t> result = std::unexpected{P2_E_PDA};
  for (const auto address : addresses) {
    if (auto read = read_aux(address, data); !read) {
      result = std::unexpected{read.error()};
      break;
    }
    if (!p2_validate_pda(data.data())) {
      result = address;
      break;
    }
  }
  const IgnoreCancellation cleanup_scope{bus_};
  const auto cleanup = enable_aux(false);
  if (result && !cleanup)
    return std::unexpected{cleanup.error()};
  return result;
}
CardStatus Card::reset(uint8_t cor, uint8_t hcr) {
  if (auto result = alive(); !result)
    return result;
  bus_.write_cfg(bus_.ctx, P2_CFG_COR, static_cast<uint8_t>(cor | 0x80));
  bus_.delay_us(bus_.ctx, 10000);
  if (auto result = alive(); !result)
    return result;
  bus_.write_cfg(bus_.ctx, P2_CFG_CCSR, hcr);
  bus_.delay_us(bus_.ctx, 10000);
  if (auto result = alive(); !result)
    return result;
  bus_.write_cfg(bus_.ctx, P2_CFG_COR, static_cast<uint8_t>(cor & 0x7f));
  bus_.delay_us(bus_.ctx, 10000);
  return alive();
}
ProbeReport Card::probe(unsigned bits) {
  constexpr std::array<uint8_t, 4> pattern{0x00, 0xe1, 0xa1, 0xff};
  ProbeReport report;
  report.sram_bits = bits;
  const auto perform = [&]() -> CardStatus {
    if (bits != 8 && bits != 16)
      return std::unexpected{P2_E_ARGUMENT};
    if (auto result = interrupted(); !result)
      return result;
    report.cor_before = bus_.read_cfg(bus_.ctx, P2_CFG_COR);
    report.ccsr_before = bus_.read_cfg(bus_.ctx, P2_CFG_CCSR);
    if (auto result = enable_aux(true); !result)
      return result;
    // Once AUX opened, every exit below must restore normal mode.
    auto exercise = [&]() -> CardStatus {
      bus_.write_cfg(bus_.ctx, P2_CFG_COR,
                     static_cast<uint8_t>(report.cor_before | 0x80));
      bus_.delay_us(bus_.ctx, 2000);
      if (auto result = interrupted(); !result)
        return result;
      bus_.write_cfg(bus_.ctx, P2_CFG_COR,
                     static_cast<uint8_t>(report.cor_before & 0x7f));
      bus_.delay_us(bus_.ctx, 2000);
      if (auto result = write_aux(P2_GENESIS_ADDRESS, pattern); !result)
        return result;
      if (auto result = reset(report.cor_before, bits == 8 ? 0x1f : 0x0f);
          !result)
        return result;
      std::array<uint8_t, 4> discard;
      if (auto result = read_aux(P2_GENESIS_ADDRESS, discard); !result)
        return result;
      if (auto result = write_aux(P2_GENESIS_ADDRESS, pattern); !result)
        return result;
      if (auto result = read_aux(P2_GENESIS_ADDRESS, report.readback); !result)
        return result;
      if (pattern != report.readback)
        return std::unexpected{P2_E_VERIFY};
      return {};
    };
    auto result = exercise();
    const bool cancelled = bus_.cancelled && bus_.cancelled(bus_.ctx);
    const IgnoreCancellation cleanup_scope{bus_};
    auto cleanup = reset(report.cor_before, bits == 8 ? 0x17 : 0x07);
    if (cleanup)
      cleanup = enable_aux(false);
    report.cleanup_error = cleanup ? P2_OK : cleanup.error();
    if (result && cancelled)
      return std::unexpected{P2_E_CANCELLED};
    return result ? cleanup : result;
  };
  const auto result = perform();
  report.error = result ? P2_OK : result.error();
  return report;
}
} // namespace p2
