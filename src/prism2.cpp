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
    const auto value = bus_.read16(bus_.ctx, reg);
    if (auto result = interrupted(); !result)
      return result;
    if ((value & mask) == expected)
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
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(bus_.ctx, P2_PARAM0, enable ? 0xfe01 : 0);
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(bus_.ctx, P2_PARAM1, enable ? 0xdc23 : 0);
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(bus_.ctx, P2_PARAM2, enable ? 0xba45 : 0);
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(
      bus_.ctx, P2_CONTROL,
      static_cast<uint16_t>((control & 0x3fff) | (enable ? 0x8000 : 0x4000)));
  bus_.delay_us(bus_.ctx, 5);
  return wait_mask(P2_CONTROL, 0xc000, enable ? 0xc000 : 0, 100000);
}
CardStatus Card::transfer(uint32_t address, std::span<uint8_t> read,
                          std::span<const uint8_t> write, uint16_t space,
                          std::optional<uint32_t> started) {
  auto length = write.empty() ? read.size() : write.size();
  // AUXPAGE is 16 bits and its offset 7 bits. Never silently truncate.
  if ((address & 1) || (length & 1) || address >= 0x800000 ||
      length > 0x800000 - address)
    return std::unexpected{P2_E_ARGUMENT};
  while (length) {
    if (auto result = interrupted(); !result)
      return result;
    if (started &&
        static_cast<uint32_t>(bus_.now_us(bus_.ctx) - *started) >= 30000000)
      return std::unexpected{P2_E_TIMEOUT};
    const auto count = std::min<std::size_t>(length, 128 - (address & 127));
    bus_.write16(bus_.ctx, P2_AUXPAGE, static_cast<uint16_t>(address >> 7));
    if (auto result = interrupted(); !result)
      return result;
    bus_.write16(bus_.ctx, P2_AUXOFFSET,
                 static_cast<uint16_t>((address & 127) | space));
    bus_.delay_us(bus_.ctx, 5);
    for (std::size_t i = 0; i < count; i += 2) {
      if (auto result = interrupted(); !result)
        return result;
      if (started &&
          static_cast<uint32_t>(bus_.now_us(bus_.ctx) - *started) >= 30000000)
        return std::unexpected{P2_E_TIMEOUT};
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
CardStatus Card::seek_nv_byte(uint32_t address) {
  if (address >= 0x800000)
    return std::unexpected{P2_E_ARGUMENT};
  if (!bus_.read8 || !bus_.write8)
    return std::unexpected{P2_E_UNSUPPORTED};
  if (auto result = interrupted(); !result)
    return result;
  // ILHOPFW 0a22:02f9 direct seek retains bit zero in AUXOFFSET, then
  // 0a22:000a/004f selects AUXDATA+1 for an odd byte address.
  bus_.write16(bus_.ctx, P2_AUXPAGE, static_cast<uint16_t>(address >> 7));
  if (auto result = interrupted(); !result)
    return result;
  bus_.write16(bus_.ctx, P2_AUXOFFSET,
               static_cast<uint16_t>((address & 127) | 0x1000));
  bus_.delay_us(bus_.ctx, 5);
  return interrupted();
}
CardResult<uint8_t> Card::read_nv_byte(uint32_t address) {
  if (auto result = seek_nv_byte(address); !result)
    return std::unexpected{result.error()};
  const auto value = bus_.read8(bus_.ctx, P2_AUXDATA + (address & 1));
  if (auto result = interrupted(); !result)
    return std::unexpected{result.error()};
  return value;
}
CardStatus Card::write_nv_byte(uint32_t address, uint8_t value) {
  if (auto result = seek_nv_byte(address); !result)
    return result;
  bus_.write8(bus_.ctx, P2_AUXDATA + (address & 1), value);
  return interrupted();
}
CardStatus Card::write_aux(uint32_t address, std::span<const uint8_t> data) {
  return transfer(address, {}, data, 0);
}
WriteReport Card::write_verified(uint32_t address,
                                 std::span<const uint8_t> data) {
  WriteReport report;
  report.address = report.last_attempted_address = address;
  report.bytes_requested = data.size();
  if (data.empty() || (address & 1) || (data.size() & 1) ||
      address >= 0x800000 || data.size() > 0x800000 - address) {
    report.error = P2_E_ARGUMENT;
    return report;
  }
  const auto start = bus_.now_us(bus_.ctx);
  const auto deadline = [&]() -> CardStatus {
    if (static_cast<uint32_t>(bus_.now_us(bus_.ctx) - start) >= 30000000)
      return std::unexpected{P2_E_TIMEOUT};
    return interrupted();
  };
  const auto perform = [&]() -> CardStatus {
    std::array<uint8_t, 128> previous{};
    while (!data.empty()) {
      if (auto result = deadline(); !result)
        return result;
      const auto count =
          std::min<std::size_t>(data.size(), 128 - (address & 127));
      const auto expected = data.first(count);
      report.last_attempted_address = address;
      report.write_started = true;
      if (auto result = transfer(address, {}, expected, 0, start); !result)
        return result;
      // Whole successful transfers only; a failed transfer can have changed a
      // prefix. Never automatically rewrite a partially transferred chunk.
      report.bytes_written += count;
      bool verified = false;
      for (unsigned attempt = 0; attempt < 20; ++attempt) {
        if (auto result = deadline(); !result)
          return result;
        auto readback = std::span{report.last_readback}.first(count);
        if (auto result = transfer(address, readback, {}, 0, start); !result)
          return result;
        ++report.verification_reads;
        if (auto result = deadline(); !result)
          return result;
        if (attempt) {
          const auto stable =
              std::equal(readback.begin(), readback.end(), previous.begin());
          if (!stable)
            ++report.disagreements;
          if (stable &&
              std::equal(readback.begin(), readback.end(), expected.begin())) {
            verified = true;
            break;
          }
        }
        std::copy(readback.begin(), readback.end(), previous.begin());
      }
      if (!verified)
        return std::unexpected{P2_E_VERIFY};
      report.bytes_verified += count;
      address += static_cast<uint32_t>(count);
      data = data.subspan(count);
    }
    return {};
  };
  const auto result = perform();
  report.error = result ? P2_OK : result.error();
  report.elapsed_us = bus_.now_us(bus_.ctx) - start;
  return report;
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
  // Original ILHOPFW 0a22:0ad5: Genesis latches HCR immediately while reset
  // is asserted; normal return waits 5 ms before changing HCR.
  if (!(hcr & 8))
    bus_.delay_us(bus_.ctx, 5000);
  if (auto result = alive(); !result)
    return result;
  bus_.write_cfg(bus_.ctx, P2_CFG_CCSR, hcr);
  bus_.delay_us(bus_.ctx, 5000);
  if (auto result = alive(); !result)
    return result;
  bus_.write_cfg(bus_.ctx, P2_CFG_COR, static_cast<uint8_t>(cor & 0x7f));
  // Allow a bounded Gayle settling interval after deassertion.
  bus_.delay_us(bus_.ctx, 5000);
  return alive();
}
CardStatus Card::restore_aux(bool was_enabled) {
  // Scratch pointers are diagnostic only. Clear OFFSET while AUX is enabled,
  // before any disable request; never replay PARAM/PAGE/OFFSET snapshots.
  if (auto result = alive(); !result)
    return result;
  const auto control = bus_.read16(bus_.ctx, P2_CONTROL);
  if (auto result = alive(); !result)
    return result;
  if (control == 0xffff)
    return std::unexpected{P2_E_IO};
  if ((control & 0xc000) == 0xc000)
    bus_.write16(bus_.ctx, P2_AUXOFFSET, 0);
  if ((control & 0xc000) == (was_enabled ? 0xc000 : 0))
    return alive();
  return enable_aux(was_enabled);
}
ProbeReport Card::probe(unsigned bits) {
  constexpr std::array<uint8_t, 4> pattern{0x00, 0xe1, 0xa1, 0xff};
  ProbeReport report;
  report.sram_bits = bits;
  if (bits != 8 && bits != 16) {
    report.error = P2_E_ARGUMENT;
    return report;
  }
  bool aux_acquired = false;
  const auto exercise = [&]() -> CardStatus {
    if (auto result = interrupted(); !result)
      return result;
    const auto control = bus_.read16(bus_.ctx, P2_CONTROL);
    if (auto result = interrupted(); !result)
      return result;
    if (control == 0xffff ||
        ((control & 0xc000) != 0 && (control & 0xc000) != 0xc000))
      return std::unexpected{P2_E_IO};
    report.aux_was_enabled = (control & 0xc000) == 0xc000;
    report.cor_before = bus_.read_cfg(bus_.ctx, P2_CFG_COR);
    if (auto result = interrupted(); !result)
      return result;
    report.ccsr_before = bus_.read_cfg(bus_.ctx, P2_CFG_CCSR);
    if (auto result = interrupted(); !result)
      return result;
    if ((report.cor_before & 0x80) || report.ccsr_before == 0xff)
      return std::unexpected{P2_E_IO};
    // The original DOS routine writes literal C5/45, independent of the
    // previous COR or the CIS tuple index. The native x16 path follows it.
    // x8 remains a Host AP model experiment, unavailable on native hardware.
    report.cor_used = bits == 16 ? 0x45 : report.cor_before;
    aux_acquired = true; // Also restore AUX after a failed enable handshake.
    if (!report.aux_was_enabled) {
      if (auto result = enable_aux(true); !result)
        return result;
    }
    if (auto result = interrupted(); !result)
      return result;
    // DOS ILHOPFW reset dispatcher (0a22:0ad5) seeds/checks the idle words
    // before the first HCR latch; there is no preliminary bare COR pulse.
    // Its first idle result is advisory; the check after the first latch is
    // required. AUX has already been acquired explicitly here.
    if (auto result = write_aux(P2_GENESIS_ADDRESS, pattern); !result)
      return result;
    std::array<uint8_t, 4> discard;
    if (auto result = read_aux(P2_GENESIS_ADDRESS, discard); !result)
      return result;
    report.reset_started = true;
    if (auto result = reset(report.cor_used, bits == 8 ? 0x1f : 0x0f); !result)
      return result;
    ++report.genesis_latches;
    const auto written = write_verified(P2_GENESIS_ADDRESS, pattern);
    std::copy_n(written.last_readback.begin(), report.readback.size(),
                report.readback.begin());
    report.verification_reads = written.verification_reads;
    report.disagreements = written.disagreements;
    if (written.error)
      return std::unexpected{written.error};
    // The DOS routine performs a second HCR latch after the successful idle
    // test. Verification retries above never repeat a payload write.
    if (auto result = reset(report.cor_used, bits == 8 ? 0x1f : 0x0f); !result)
      return result;
    ++report.genesis_latches;
    return {};
  };
  const auto result = exercise();
  const auto cancellation = bus_.cancelled;
  // Cleanup cannot be vetoed by Ctrl-C; sticky removal still forbids I/O.
  const IgnoreCancellation cleanup_scope{bus_};
  CardStatus normal;
  if (report.reset_started)
    normal = reset(report.cor_used, bits == 8 ? 0x17 : 0x07);
  // DOS's literal configuration index 5 can change the Amiga I/O access
  // mode. Restore this insertion's original configuration BEFORE any AUX
  // cleanup, INIT or post-capture word reads. This is not another reset.
  if (report.reset_started && normal && report.cor_used != report.cor_before) {
    normal = alive();
    if (normal) {
      bus_.write_cfg(bus_.ctx, P2_CFG_COR, report.cor_before);
      bus_.delay_us(bus_.ctx, 5000);
      normal = alive();
      if (normal && bus_.read_cfg(bus_.ctx, P2_CFG_COR) != report.cor_before)
        normal = std::unexpected{P2_E_VERIFY};
      if (normal)
        normal = alive();
    }
    report.io_configuration_restored = normal.has_value();
  }
  CardStatus aux;
  if (aux_acquired)
    aux = restore_aux(report.aux_was_enabled);
  report.aux_cleanup_error = aux ? P2_OK : aux.error();
  report.cleanup_error = normal ? report.aux_cleanup_error : normal.error();
  const bool cancelled = cancellation && cancellation(bus_.ctx);
  report.error = result ? (report.cleanup_error ? report.cleanup_error
                           : cancelled          ? P2_E_CANCELLED
                                                : P2_OK)
                        : result.error();
  return report;
}
} // namespace p2
