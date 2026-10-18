/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "owned_memory.h"
#include "prism2.h"
#include <string_view>

namespace p2::preflight {
// AmigaDOS DateStamp is local time since 1978-01-01, 50 ticks per second.
struct Clock {
  int32_t days = 0, minutes = 0, ticks = 0;
};
// Offset is explicitly supplied minutes EAST of UTC. It is not guessed from
// the host, locale, or a DateStamp; matching checks consistency, not NTP truth.
[[nodiscard]] CardResult<int64_t> utc_seconds(std::string_view utc);
[[nodiscard]] CardResult<int64_t> check_clock(Clock local, int offset_east,
                                              std::string_view supplied_utc);
struct Volume {
  uint32_t disk_type = 0;
  int32_t disk_state = 0, blocks = 0, used = 0, bytes_per_block = 0;
  bool same_as_ram = true;
};
inline constexpr uint64_t RequiredBytes = 4ull * 0x800000 + 65536;
[[nodiscard]] CardStatus check_volume(const Volume &);
struct Evidence {
  Clock clock;
  Volume volume;
  int offset_east = 0;
  int64_t checked_utc_seconds = 0;
  std::array<char, 384> resolved_directory{};
};
// Resolve/check a new destination without creating it. Used by experimental
// acquisition as well as diagnostic preflight; it grants no operation
// authority.
[[nodiscard]] CardResult<Owned<Evidence>> destination(const char *directory);
// Read filesystem/clock only. No card resource, directory creation or files.
[[nodiscard]] CardResult<Owned<Evidence>>
native(const char *directory, const char *utc, int offset_east);
int run_command(const char *directory, const char *utc, const char *offset);
} // namespace p2::preflight
