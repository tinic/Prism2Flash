/* SPDX-License-Identifier: GPL-2.0-only
 * Firmware DOWNLOAD sequence: ILHOPFW 0.5e 0A22:1A79 mode 2 and Host AP
 * hostap_download.c, Jouni Malinen. FD01=buffer, FD00=maximum load time.
 */
#include "host_flash.h"
#include <algorithm>
namespace p2::flash {
Report program_firmware(Card &card, p2_bus &bus,
                        std::span<const uint8_t> before,
                        std::span<const uint8_t> desired, uint32_t base) {
  Report report;
  if (before.size() != Bytes || desired.size() != Bytes ||
      (base != 0x7e0000 && base != 0x3e0000)) {
    report.error = P2_E_ARGUMENT;
    return report;
  }
  if (!std::equal(before.begin() + 0xfe00, before.begin() + 0x10800,
                  desired.begin() + 0xfe00)) {
    report.error = P2_E_VERIFY;
    return report;
  }
  bool aux_opened = false;
  const auto perform = [&]() -> CardStatus {
    std::array<uint16_t, 3> buffer{};
    std::array<uint16_t, 1> maximum{};
    if (auto r = card.record(0xfd01, buffer); !r)
      return r;
    if (auto r = card.record(0xfd00, maximum); !r)
      return r;
    const uint32_t address = (uint32_t(buffer[0]) << 7) + buffer[1];
    const uint32_t size = std::min<uint32_t>(buffer[2], 4096);
    if (buffer[1] >= 128 || (address & 1) || !size || (size & 1) ||
        address >= 0x800000 || size > 0x800000 - address)
      return std::unexpected{P2_E_UNSUPPORTED};
    const uint32_t timeout = std::max<uint32_t>(maximum[0], 5000) * 1000;
    const auto control = card.aux_control();
    if (!control)
      return std::unexpected{control.error()};
    if ((*control & 0xc000) != 0xc000) {
      if (auto r = card.enable_aux(true); !r)
        return r;
      aux_opened = true;
    }
    std::array<uint8_t, 128> readback{};
    for (uint32_t offset = 0; offset < Bytes; offset += readback.size()) {
      if (auto r = card.read_nv(base + offset, readback); !r)
        return r;
      if (!std::equal(readback.begin(), readback.end(),
                      before.begin() + offset))
        return std::unexpected{P2_E_VERIFY};
    }
    if (std::equal(before.begin(), before.end(), desired.begin()))
      return {};
    // Write the whole preserved image, including unchanged gaps. Firmware
    // controls its flash algorithm; the host never guesses a chip erase mode.
    for (uint32_t offset = 0; offset < Bytes; offset += size) {
      const auto count = uint16_t(std::min<uint32_t>(size, Bytes - offset));
      report.last_address = base + offset;
      report.recovery_required = true;
      if (auto r = card.download(2, base + offset, count, timeout); !r)
        return r;
      const auto written =
          card.write_verified(address, desired.subspan(offset, count));
      if (written.error)
        return std::unexpected{written.error};
      report.program_command_attempted = true;
      if (auto r = card.download(3, 0, 0, timeout); !r)
        return r;
      report.programmed_bytes += count;
      for (uint32_t part = 0; part < count; part += readback.size()) {
        auto data = std::span{readback}.first(
            std::min<size_t>(readback.size(), count - part));
        if (auto r = card.read_nv(base + offset + part, data); !r)
          return r;
        if (!std::equal(data.begin(), data.end(),
                        desired.begin() + offset + part))
          return std::unexpected{P2_E_VERIFY};
        report.verified_bytes += data.size();
      }
    }
    // Verify again after all chunks to detect collateral changes to old chunks.
    for (uint32_t offset = 0; offset < Bytes; offset += readback.size()) {
      if (auto r = card.read_nv(base + offset, readback); !r)
        return r;
      if (!std::equal(readback.begin(), readback.end(),
                      desired.begin() + offset))
        return std::unexpected{P2_E_VERIFY};
    }
    // DOS final 0x22 ends DOWNLOAD and lets the firmware restart. No retry.
    if (auto r = card.download(0, 0, 0, timeout); !r)
      return r;
    bus.delay_us(bus.ctx, 100000);
    report.recovery_required = false;
    return {};
  };
  auto result = perform();
  // DOS waits 100 ms after DOWNLOAD and closes only AUX it enabled.
  // Never force AUX back on after 0022; the after-capture owns its session.
  // Keep uncertain DOWNLOAD/programming sessions untouched for recovery.
  if (aux_opened && !report.recovery_required) {
    auto cleanup_bus = bus;
    cleanup_bus.cancelled = nullptr;
    Card cleanup_card{cleanup_bus};
    const auto control = cleanup_card.aux_control();
    const auto cleanup = !control ? CardStatus{std::unexpected{control.error()}}
                         : (*control & 0xc000) == 0xc000
                             ? cleanup_card.enable_aux(false)
                             : CardStatus{};
    if (result && !cleanup)
      result = cleanup;
  }
  report.error = result ? P2_OK : result.error();
  if (report.error && report.program_command_attempted)
    report.recovery_required = true;
  return report;
}
} // namespace p2::flash
