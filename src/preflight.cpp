/* SPDX-License-Identifier: GPL-2.0-only */
#include "preflight.h"
#include "backup.h"
#include "p2_stdio.h"
#include <charconv>
#include <cstring>

namespace p2::preflight {
namespace {
CardResult<int64_t> clock_seconds(Clock local, int offset_east) {
  if (offset_east == LocalTime)
    offset_east = 0;
  if (offset_east < -840 || offset_east > 840 || local.days < 0 ||
      local.days > 44559 || local.minutes < 0 || local.minutes >= 1440 ||
      local.ticks < 0 || local.ticks >= 3000)
    return std::unexpected{P2_E_ARGUMENT};
  return int64_t(local.days) * 86400 + local.minutes * 60 + local.ticks / 50 -
         int64_t(offset_east) * 60;
}
bool leap(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
CardResult<std::array<char, 17>> format_utc(int64_t seconds,
                                            unsigned min_year) {
  if (seconds < 0)
    return std::unexpected{P2_E_ARGUMENT};
  auto days = seconds / 86400;
  const auto within = unsigned(seconds % 86400);
  unsigned year = 1978, month = 1;
  for (; year <= 2099 && days >= 365 + leap(year); ++year)
    days -= 365 + leap(year);
  constexpr std::array<unsigned, 12> lengths{31, 28, 31, 30, 31, 30,
                                             31, 31, 30, 31, 30, 31};
  for (; month < 12; ++month) {
    const auto count = lengths[month - 1] + (month == 2 && leap(year));
    if (days < count)
      break;
    days -= count;
  }
  std::array<char, 17> result{};
  if (year < min_year || year > 2099 ||
      p2_snprintf(result.data(), result.size(), "%04u%02u%02uT%02u%02u%02uZ",
                  year, month, unsigned(days + 1), within / 3600,
                  within % 3600 / 60, within % 60) != 16)
    return std::unexpected{P2_E_ARGUMENT};
  return result;
}
} // namespace
std::array<char, 17> clock_label(Clock local, int east) {
  const auto seconds = clock_seconds(local, east);
  if (seconds) {
    auto result = format_utc(*seconds, 1978);
    if (result) {
      if (east == LocalTime)
        (*result)[15] = 'L';
      return *result;
    }
  }
  std::array<char, 17> result{};
  std::strcpy(result.data(), "untimed");
  return result;
}
CardStatus check_volume(const Volume &v) {
  // Fail closed on unknown/RAM/custom filesystem types. Add a filesystem only
  // after independent review; names/assign spelling do not prove persistence.
  const bool dos = v.disk_type >= 0x444f5300 && v.disk_type <= 0x444f5308;
  if (v.same_as_ram || !dos || v.disk_state != 82 || v.blocks <= 0 ||
      v.used < 0 || v.used > v.blocks || v.bytes_per_block <= 0)
    return std::unexpected{P2_E_UNSUPPORTED};
  const auto free = uint64_t(v.blocks - v.used) * uint32_t(v.bytes_per_block);
  return free >= RequiredBytes ? CardStatus{}
                               : CardStatus{std::unexpected{P2_E_IO}};
}
int run_command(const char *directory, const char *utc, const char *offset) {
  int east = 0;
  if (!directory || !utc || !offset)
    return 20;
  const char *end = offset + std::strlen(offset);
  const auto parsed = std::from_chars(offset, end, east);
  if (parsed.ec != std::errc{} || parsed.ptr != end || parsed.ptr == offset) {
    puts("Native preflight rejected: invalid UTC offset; no card access was "
         "attempted.");
    return 20;
  }
  auto evidence = native(directory, utc, east);
  if (!evidence) {
    p2_printf("Native preflight rejected: %s; no card access was attempted.\n",
              p2_error_string(evidence.error()));
    return 20;
  }
  p2_printf(
      "Native storage preflight passed: %s\n"
      "Backup labels only: AmigaDOS clock as set, optional offset east=%d "
      "minutes.\n"
      "Physical profile unapproved; no operation authorized; no card "
      "access was attempted.\n",
      (*evidence)->resolved_directory.data(), east);
  return 0;
}
} // namespace p2::preflight
