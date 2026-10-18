/* SPDX-License-Identifier: GPL-2.0-only */
#include "bootstrap.h"
#include <algorithm>

namespace p2::bootstrap {
Report load(Card &card, p2_bus &bus, const firmware::InitialImage &image,
            unsigned sram_bits) {
  Report report;
  report.genesis = card.enter_genesis(sram_bits ? sram_bits : 8);
  // Host AP prism2_download_genesis: try x8, then x16 only on RAM mismatch.
  if (!sram_bits && report.genesis.error == P2_E_VERIFY)
    report.genesis = card.enter_genesis(16);
  report.recovery_required = report.genesis.reset_started;
  if (report.genesis.error) {
    report.error = report.genesis.error;
    return report;
  }
  const auto perform = [&]() -> CardStatus {
    // Scratch is outside the pinned initial image's payload. The real card
    // must prove both true byte-write lanes before JEDEC can be considered.
    if (auto r = card.test_ram_byte_access(0x7e0070); !r)
      return r;
    report.byte_writes_verified = true;
    std::array<uint8_t, 128> readback{};
    for (const auto &segment : image.segments()) {
      report.last_address = segment.address;
      bool equal = true;
      for (std::size_t offset = 0; offset < segment.data.size();
           offset += 128) {
        const auto count =
            std::min<std::size_t>(128, segment.data.size() - offset);
        auto buffer = std::span{readback}.first(count);
        if (auto r = card.read_aux(segment.address + offset, buffer); !r)
          return r;
        equal &= std::equal(buffer.begin(), buffer.end(),
                            segment.data.bytes().begin() + offset);
      }
      if (equal) {
        report.bytes_skipped += segment.data.size();
        continue;
      }
      bus.delay_us(bus.ctx, 50000); // DOS loader 0a22:1dbb.
      const auto written =
          card.write_verified(segment.address, segment.data.bytes());
      report.bytes_written += written.bytes_written;
      report.bytes_verified += written.bytes_verified;
      if (written.error)
        return std::unexpected{written.error};
    }
    report.genesis = card.leave_genesis(report.genesis);
    if (report.genesis.error)
      return std::unexpected{report.genesis.error};
    if (auto r = card.ensure_aux_enabled(); !r)
      return r;
    report.initial_started = true;
    report.recovery_required = false;
    return {};
  };
  const auto result = perform();
  report.error = result ? P2_OK : result.error();
  return report;
}
} // namespace p2::bootstrap
