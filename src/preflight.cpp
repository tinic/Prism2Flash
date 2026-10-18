/* SPDX-License-Identifier: GPL-2.0-only */
#include "preflight.h"
#include "backup.h"
#include "p2_stdio.h"
#include <charconv>
#include <cstring>

namespace p2::preflight {
CardResult<int64_t> utc_seconds(std::string_view utc) {
  if (!backup::valid_utc(utc))
    return std::unexpected{P2_E_ARGUMENT};
  auto number = [&](size_t start, size_t count) {
    unsigned value = 0;
    for (size_t i = start; i < start + count; ++i)
      value = value * 10 + utc[i] - '0';
    return value;
  };
  auto leap = [](unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  };
  int64_t days = 0;
  unsigned year = number(0, 4), month = number(4, 2);
  for (unsigned y = 1978; y < year; ++y)
    days += 365 + leap(y);
  constexpr std::array<unsigned, 12> lengths{31, 28, 31, 30, 31, 30,
                                             31, 31, 30, 31, 30, 31};
  for (unsigned m = 1; m < month; ++m)
    days += lengths[m - 1] + (m == 2 && leap(year));
  days += number(6, 2) - 1;
  return days * 86400 + number(9, 2) * 3600 + number(11, 2) * 60 +
         number(13, 2);
}
CardResult<int64_t> check_clock(Clock local, int offset_east,
                                std::string_view supplied) {
  auto reference = utc_seconds(supplied);
  if (!reference || offset_east < -840 || offset_east > 840 || local.days < 0 ||
      local.days > 44559 || local.minutes < 0 || local.minutes >= 1440 ||
      local.ticks < 0 || local.ticks >= 3000)
    return std::unexpected{P2_E_ARGUMENT};
  const int64_t actual = int64_t(local.days) * 86400 + local.minutes * 60 +
                         local.ticks / 50 - int64_t(offset_east) * 60;
  const auto skew = actual - *reference;
  if (skew < -120 || skew > 120)
    return std::unexpected{P2_E_VERIFY};
  return actual;
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
  p2_printf("Native storage/clock preflight passed: %s\n"
            "Clock basis: AmigaDOS local DateStamp; operator offset east=%d "
            "minutes.\n"
            "Physical profile unapproved; no operation authorized; no card "
            "access was attempted.\n",
            (*evidence)->resolved_directory.data(), east);
  return 0;
}
} // namespace p2::preflight
