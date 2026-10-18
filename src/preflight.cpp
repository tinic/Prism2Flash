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
CardResult<std::array<char, 17>> format_utc(int64_t seconds) {
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
  if (year < 2020 || year > 2099 ||
      p2_snprintf(result.data(), result.size(), "%04u%02u%02uT%02u%02u%02uZ",
                  year, month, unsigned(days + 1), within / 3600,
                  within % 3600 / 60, within % 60) != 16)
    return std::unexpected{P2_E_ARGUMENT};
  return result;
}
} // namespace
CardResult<std::array<char, 17>> format_clock(Clock local, int offset_east) {
  const auto seconds = clock_seconds(local, offset_east);
  if (!seconds)
    return std::unexpected{seconds.error()};
  auto result = format_utc(*seconds);
  if (result && offset_east == LocalTime)
    (*result)[15] = 'L';
  return result;
}
bool valid_timestamp(std::string_view timestamp, int offset_east) {
  if (!valid_offset(offset_east) || timestamp.size() != 16)
    return false;
  if (offset_east != LocalTime)
    return backup::valid_utc(timestamp);
  if (timestamp[15] != 'L')
    return false;
  std::array<char, 17> calendar{};
  std::copy(timestamp.begin(), timestamp.end(), calendar.begin());
  calendar[15] = 'Z';
  return backup::valid_utc(calendar.data());
}
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
  if (!valid_timestamp(supplied, offset_east))
    return std::unexpected{P2_E_ARGUMENT};
  std::array<char, 17> calendar{};
  std::copy(supplied.begin(), supplied.end(), calendar.begin());
  calendar[15] = 'Z';
  auto reference = utc_seconds(calendar.data());
  auto actual = clock_seconds(local, offset_east);
  if (!reference || !actual)
    return std::unexpected{P2_E_ARGUMENT};
  const auto skew = *actual - *reference;
  if (skew < -120 || skew > 120)
    return std::unexpected{P2_E_VERIFY};
  return *actual;
}
CardResult<std::array<char, 17>> Timeline::stamp(Checkpoint point, Clock local,
                                                 int east,
                                                 std::string_view reference,
                                                 uint32_t now) {
  auto fail = [&](p2_error error) -> CardResult<std::array<char, 17>> {
    failed_ = true;
    return std::unexpected{error};
  };
  if (failed_ || unsigned(point) != next_ || next_ >= 4)
    return fail(P2_E_ARGUMENT);
  auto utc = clock_seconds(local, east);
  if (!utc)
    return fail(utc.error());
  if (!next_) {
    if (auto checked = check_clock(local, east, reference); !checked)
      return fail(checked.error());
    first_utc_ = *utc;
    first_us_ = now;
  }
  const uint32_t elapsed = now - first_us_;
  const auto skew = *utc - (first_utc_ + elapsed / 1000000);
  if (elapsed > 1800000000u || elapsed < last_elapsed_ || skew < -2 ||
      skew > 2 || (next_ && *utc < last_utc_))
    return fail(P2_E_VERIFY);
  auto text = format_clock(local, east);
  if (!text)
    return fail(text.error());
  last_utc_ = *utc;
  last_elapsed_ = elapsed;
  ++next_;
  return text;
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
