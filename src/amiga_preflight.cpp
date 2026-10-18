/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "p2_stdio.h"
#include "preflight.h"
#include <cstring>
#include <dos/dos.h>
#include <proto/dos.h>

namespace p2::preflight {
namespace {
class LockHandle {
public:
  explicit LockHandle(const char *name)
      : value_(Lock((CONST_STRPTR)name, ACCESS_READ)) {}
  ~LockHandle() {
    if (value_)
      UnLock(value_);
  }
  LockHandle(const LockHandle &) = delete;
  LockHandle &operator=(const LockHandle &) = delete;
  BPTR get() const { return value_; }

private:
  BPTR value_;
};
} // namespace
CardResult<Evidence> destination(const char *directory) {
  if (!directory || std::strlen(directory) > 320)
    return std::unexpected{P2_E_ARGUMENT};
  const char *colon = std::strchr(directory, ':');
  const char *slash = std::strrchr(directory, '/');
  if (!colon || (slash && slash < colon) || std::strchr(colon + 1, ':'))
    return std::unexpected{P2_E_ARGUMENT};
  const char *leaf = (slash ? slash : colon) + 1;
  if (!*leaf)
    return std::unexpected{P2_E_ARGUMENT};
  for (const char *p = leaf; *p; ++p)
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '-' || *p == '_'))
      return std::unexpected{P2_E_ARGUMENT};
  std::array<char, 384> parent{};
  std::memcpy(parent.data(), directory, size_t(leaf - directory));
  LockHandle parent_lock{parent.data()}, ram{"RAM:"};
  if (!parent_lock.get() || !ram.get())
    return std::unexpected{P2_E_IO};
  Evidence result;
  alignas(4) InfoData disk{};
  if (!Info(parent_lock.get(), &disk))
    return std::unexpected{P2_E_IO};
  result.volume =
      Volume{uint32_t(disk.id_DiskType),
             disk.id_DiskState,
             disk.id_NumBlocks,
             disk.id_NumBlocksUsed,
             disk.id_BytesPerBlock,
             SameLock(parent_lock.get(), ram.get()) != LOCK_DIFFERENT};
  if (auto checked = check_volume(result.volume); !checked)
    return std::unexpected{checked.error()};
  // Resolve assigns via a lock; later output paths use the canonical volume
  // path. A RAM assign is refused even when its spelling is unrelated to RAM.
  if (!NameFromLock(parent_lock.get(), (STRPTR)parent.data(), parent.size()))
    return std::unexpected{P2_E_IO};
  const auto length = std::strlen(parent.data());
  const auto count =
      p2_snprintf(result.resolved_directory.data(),
                  result.resolved_directory.size(), "%s%s%s", parent.data(),
                  length && parent[length - 1] == ':' ? "" : "/", leaf);
  if (count <= 0 || size_t(count) >= result.resolved_directory.size())
    return std::unexpected{P2_E_ARGUMENT};
  {
    LockHandle existing{result.resolved_directory.data()};
    if (existing.get() || IoErr() != ERROR_OBJECT_NOT_FOUND)
      return std::unexpected{P2_E_IO};
  }
  return result;
}
CardResult<Evidence> native(const char *directory, const char *utc, int east) {
  if (!directory || !utc || !backup::valid_utc(utc) ||
      !std::strstr(directory, utc) || east < -840 || east > 840)
    return std::unexpected{P2_E_ARGUMENT};
  auto resolved = destination(directory);
  if (!resolved)
    return std::unexpected{resolved.error()};
  auto &result = *resolved;
  struct DateStamp stamp {};
  DateStamp(&stamp);
  result.clock = Clock{stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick};
  auto checked = check_clock(result.clock, east, utc);
  if (!checked)
    return std::unexpected{checked.error()};
  result.checked_utc_seconds = *checked;
  result.offset_east = east;
  return result;
}
} // namespace p2::preflight
