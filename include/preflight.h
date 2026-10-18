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
// Move-only DOS lock ownership. The callback keeps SDK types out of policy
// code; an Evidence object retains the resolved parent until its last use.
class DirectoryLock {
public:
  DirectoryLock() = default;
  DirectoryLock(uintptr_t value, void (*release)(uintptr_t)) noexcept
      : value_(value), release_(release) {}
  ~DirectoryLock() { reset(); }
  DirectoryLock(const DirectoryLock &) = delete;
  DirectoryLock &operator=(const DirectoryLock &) = delete;
  DirectoryLock(DirectoryLock &&other) noexcept
      : value_(std::exchange(other.value_, 0)), release_(other.release_) {}
  DirectoryLock &operator=(DirectoryLock &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, 0);
      release_ = other.release_;
    }
    return *this;
  }
  uintptr_t get() const { return value_; }

private:
  void reset() {
    if (value_)
      release_(std::exchange(value_, 0));
  }
  uintptr_t value_ = 0;
  void (*release_)(uintptr_t) = nullptr;
};
struct Evidence {
  Clock clock;
  Volume volume;
  int offset_east = 0;
  int64_t checked_utc_seconds = 0;
  std::array<char, 384> resolved_directory{};
  DirectoryLock parent;
};
enum class Checkpoint { BeforeStart, BeforeEnd, AfterStart, AfterEnd };
// Compare wall time with a monotonic elapsed interval for all four capture
// boundaries; reject jumps, reversed/duplicate phases and a >30 minute run.
class Timeline {
public:
  [[nodiscard]] CardResult<std::array<char, 17>>
  stamp(Checkpoint, Clock, int offset_east, std::string_view reference,
        uint32_t monotonic_us);

private:
  unsigned next_ = 0;
  bool failed_ = false;
  int64_t first_utc_ = 0, last_utc_ = 0;
  uint32_t first_us_ = 0, last_elapsed_ = 0;
};
// Single-process native journal context: retain parent and destination locks,
// use the destination as this CLI process's current directory for all stream
// opens/reopens, and restore the caller's directory only after streams close.
class NativeSession {
public:
  NativeSession() = default;
  ~NativeSession();
  NativeSession(const NativeSession &) = delete;
  NativeSession &operator=(const NativeSession &) = delete;
  [[nodiscard]] static CardResult<Owned<NativeSession>>
  create(const char *directory, const char *utc, int offset_east);
  [[nodiscard]] CardResult<std::array<char, 17>> stamp(Checkpoint, p2_bus &);

private:
  Owned<Evidence> evidence_;
  DirectoryLock directory_;
  uintptr_t old_directory_ = 0;
  std::array<char, 17> reference_{};
  Timeline timeline_;
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
