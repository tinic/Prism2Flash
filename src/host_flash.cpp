/* SPDX-License-Identifier: GPL-2.0-only
 * Protocol transcribed from ILHOPFW 0.5e 0a22:2fc7 / 0a22:00b0;
 * see docs/DOS-REFERENCE.md. No proprietary executable code included.
 */
#include "host_flash.h"
#include <algorithm>

namespace p2::flash {
CardResult<Identity> identify(Card &card) {
  if (auto r = card.ensure_aux_enabled(); !r)
    return std::unexpected{r.error()};
  bool command_attempted = false;
  const auto read = [&](uint32_t a, uint32_t b) -> CardResult<Identity> {
    for (const auto &[address, value] : std::array{
             std::pair{uint32_t{0}, uint8_t{0xf0}}, std::pair{a, uint8_t{0xaa}},
             std::pair{b, uint8_t{0x55}}, std::pair{a, uint8_t{0x90}}}) {
      command_attempted = true;
      if (auto r = card.write_nv_byte(0x7e0000 + address, value); !r)
        return std::unexpected{r.error()};
    }
    const auto manufacturer = card.read_nv_byte(0x7e0000);
    if (!manufacturer)
      return std::unexpected{manufacturer.error()};
    const auto device = card.read_nv_byte(0x7e0001);
    if (!device)
      return std::unexpected{device.error()};
    if (auto r = card.write_nv_byte(0x7e0000, 0xf0); !r)
      return std::unexpected{r.error()};
    return Identity{*manufacturer, *device};
  };
  const auto detect = [&]() -> CardResult<Identity> {
    auto identity = read(0x555, 0x2aa);
    if (!identity ||
        (identity->manufacturer == 1 && identity->device == 0x6e) ||
        (identity->manufacturer == 0x20 && identity->device == 0x23))
      return identity;
    return read(0x5555, 0x2aaa);
  };
  auto result = detect();
  if (!result && command_attempted &&
      card.transport().present(card.transport().ctx)) {
    // F0 is read-mode cleanup, never an erase/program retry. Ignore Ctrl-C
    // only here; presence remains the insertion's original sticky callback.
    auto cleanup_bus = card.transport();
    cleanup_bus.cancelled = nullptr;
    Card cleanup{cleanup_bus};
    if (auto restored = cleanup.write_nv_byte(0x7e0000, 0xf0); !restored)
      return std::unexpected{restored.error()};
  }
  return result;
}
Report program_sst(Card &card, p2_bus &bus, std::span<const uint8_t> before,
                   std::span<const uint8_t> desired) {
  Report report;
  if (before.size() != Bytes || desired.size() != Bytes || !bus.read8 ||
      !bus.write8) {
    report.error = P2_E_ARGUMENT;
    return report;
  }
  // Preserve the entire PDA page and the CIS region, even if an input plug
  // or signature descriptor would try to target them. Other gaps are copied
  // unchanged by the plan builder, never filled from descriptor lengths.
  if (!std::equal(before.begin() + 0xfe00, before.begin() + 0x10800,
                  desired.begin() + 0xfe00)) {
    report.error = P2_E_VERIFY;
    return report;
  }
  const auto available = [&]() -> CardStatus {
    if (!bus.present(bus.ctx))
      return std::unexpected{P2_E_REMOVED};
    if (bus.cancelled && bus.cancelled(bus.ctx))
      return std::unexpected{P2_E_CANCELLED};
    return {};
  };
  const auto write = [&](uint32_t address, uint8_t value) {
    report.last_address = address;
    return card.write_nv_byte(0x7e0000 + address, value);
  };
  const auto unlock = [&](uint32_t a, uint32_t b,
                          uint8_t command) -> CardStatus {
    if (auto r = write(a, 0xaa); !r)
      return r;
    if (auto r = write(b, 0x55); !r)
      return r;
    return write(a, command);
  };
  const auto perform = [&]() -> CardStatus {
    if (auto r = available(); !r)
      return r;
    const auto identity = identify(card);
    if (!identity) {
      report.recovery_required = true;
      return std::unexpected{identity.error()};
    }
    report.manufacturer = identity->manufacturer;
    report.device = identity->device;
    if (report.manufacturer != 0xbf || report.device != 0xd5)
      return std::unexpected{P2_E_UNSUPPORTED};
    std::array<uint8_t, 128> readback{};
    for (uint32_t address = 0; address < Bytes; address += readback.size()) {
      if (auto r = card.read_nv(0x380000 + address, readback); !r)
        return r;
      if (!std::equal(readback.begin(), readback.end(),
                      before.begin() + address))
        return std::unexpected{P2_E_VERIFY};
    }
    if (std::equal(before.begin(), before.end(), desired.begin()))
      return {}; // Already the intended complete image; no erase.
    if (auto r = available(); !r)
      return r;
    if (auto r = write(0, 0xf0); !r)
      return r;
    if (auto r = unlock(0x5555, 0x2aaa, 0x80); !r)
      return r;
    // From this point any partial command or write failure requires retaining
    // the recovery session. Never issue a blind reset or retry the erase.
    report.recovery_required = true;
    if (auto r = write(0x5555, 0xaa); !r)
      return r;
    if (auto r = write(0x2aaa, 0x55); !r)
      return r;
    report.erase_command_attempted = true;
    if (auto r = write(0x5555, 0x10); !r)
      return r;
    report.erase_started = true;
    const auto erased_at = bus.now_us(bus.ctx);
    for (;;) {
      auto value = card.read_nv_byte(0x7e0000);
      if (!value)
        return std::unexpected{value.error()};
      if (*value == 0xff)
        break;
      if (static_cast<uint32_t>(bus.now_us(bus.ctx) - erased_at) >= 10000000)
        return std::unexpected{P2_E_TIMEOUT};
      bus.delay_us(bus.ctx, 1000);
    }
    if (auto r = write(0, 0xf0); !r)
      return r;
    const auto program_at = bus.now_us(bus.ctx);
    for (uint32_t address = 0; address < Bytes; ++address) {
      if (auto r = available(); !r)
        return r;
      if (static_cast<uint32_t>(bus.now_us(bus.ctx) - program_at) >= 600000000)
        return std::unexpected{P2_E_TIMEOUT};
      if (desired[address] == 0xff)
        continue;
      if (auto r = unlock(0x5555, 0x2aaa, 0xa0); !r)
        return r;
      if (auto r = write(address, desired[address]); !r)
        return r;
      const auto written_at = bus.now_us(bus.ctx);
      for (;;) {
        auto value = card.read_nv_byte(0x7e0000 + address);
        if (!value)
          return std::unexpected{value.error()};
        if (*value == desired[address])
          break;
        if (static_cast<uint32_t>(bus.now_us(bus.ctx) - written_at) >= 1000000)
          return std::unexpected{P2_E_TIMEOUT};
        bus.delay_us(bus.ctx, 10);
      }
      ++report.programmed_bytes;
    }
    for (uint32_t address = 0; address < Bytes; address += readback.size()) {
      if (auto r = card.read_nv(0x380000 + address, readback); !r)
        return r;
      if (!std::equal(readback.begin(), readback.end(),
                      desired.begin() + address))
        return std::unexpected{P2_E_VERIFY};
      report.verified_bytes += readback.size();
    }
    report.recovery_required = false;
    return {};
  };
  const auto result = perform();
  report.error = result ? P2_OK : result.error();
  // The final word read leaves NV selected. On Gayle that can obscure
  // CONTROL and CIS, so complete the read before the caller's full capture.
  // This is scratch-register cleanup, never a flash command or reset.
  if (bus.present(bus.ctx)) {
    bus.write16(bus.ctx, P2_AUXOFFSET, 0);
    if (!bus.present(bus.ctx) && !report.error)
      report.error = P2_E_REMOVED;
  }
  if (report.error && report.erase_command_attempted)
    report.recovery_required = true;
  return report;
}
} // namespace p2::flash
