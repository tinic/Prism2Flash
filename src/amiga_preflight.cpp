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
  BPTR release() { return std::exchange(value_, 0); }

private:
  BPTR value_;
};
} // namespace
static void unlock(uintptr_t lock) { UnLock(BPTR(lock)); }
CardResult<Owned<Evidence>> destination(const char *directory) {
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
  // A separator belongs between components, not at the end of the parent
  // passed to Lock (AmigaDOS also gives empty components parent semantics).
  const auto parent_bytes = size_t((slash ? slash : leaf) - directory);
  std::memcpy(parent.data(), directory, parent_bytes);
  LockHandle parent_lock{parent.data()}, ram{"RAM:"};
  if (!parent_lock.get() || !ram.get())
    return std::unexpected{P2_E_IO};
  auto result = make_owned<Evidence>();
  if (!result)
    return std::unexpected{P2_E_IO};
  alignas(4) InfoData disk{};
  if (!Info(parent_lock.get(), &disk))
    return std::unexpected{P2_E_IO};
  result->volume =
      Volume{uint32_t(disk.id_DiskType),
             disk.id_DiskState,
             disk.id_NumBlocks,
             disk.id_NumBlocksUsed,
             disk.id_BytesPerBlock,
             SameLock(parent_lock.get(), ram.get()) != LOCK_DIFFERENT};
  if (auto checked = check_volume(result->volume); !checked)
    return std::unexpected{checked.error()};
  // Resolve assigns via a lock; later output paths use the canonical volume
  // path. A RAM assign is refused even when its spelling is unrelated to RAM.
  if (!NameFromLock(parent_lock.get(), (STRPTR)parent.data(), parent.size()))
    return std::unexpected{P2_E_IO};
  const auto length = std::strlen(parent.data());
  const auto count =
      p2_snprintf(result->resolved_directory.data(),
                  result->resolved_directory.size(), "%s%s%s", parent.data(),
                  length && parent[length - 1] == ':' ? "" : "/", leaf);
  if (count <= 0 || size_t(count) >= result->resolved_directory.size())
    return std::unexpected{P2_E_ARGUMENT};
  {
    LockHandle existing{result->resolved_directory.data()};
    if (existing.get() || IoErr() != ERROR_OBJECT_NOT_FOUND)
      return std::unexpected{P2_E_IO};
  }
  result->parent = DirectoryLock{uintptr_t(parent_lock.release()), unlock};
  return result;
}
CardResult<Owned<Evidence>> native(const char *directory, const char *utc,
                                   int east) {
  if (!directory || !utc || !valid_timestamp(utc, east) ||
      !std::strstr(directory, utc))
    return std::unexpected{P2_E_ARGUMENT};
  auto resolved = destination(directory);
  if (!resolved)
    return std::unexpected{resolved.error()};
  auto &result = **resolved;
  struct DateStamp stamp {};
  DateStamp(&stamp);
  result.clock = Clock{stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick};
  auto checked = check_clock(result.clock, east, utc);
  if (!checked)
    return std::unexpected{checked.error()};
  result.checked_utc_seconds = *checked;
  result.offset_east = east;
  return resolved;
}
NativeSession::~NativeSession() {
  if (directory_.get())
    CurrentDir(BPTR(old_directory_));
}
CardResult<Owned<NativeSession>>
NativeSession::create(const char *directory, const char *utc, int east) {
  auto checked = native(directory, utc, east);
  if (!checked)
    return std::unexpected{checked.error()};
  auto session = make_owned<NativeSession>();
  if (!session)
    return std::unexpected{P2_E_IO};
  session->evidence_ = std::move(*checked);
  std::memcpy(session->reference_.data(), utc, 16);
  // Parent remains locked while the unique child is created and throughout
  // the operation. CurrentDir is per DOS process; no assign is resolved again.
  const auto previous = CurrentDir(BPTR(session->evidence_->parent.get()));
  const char *path = session->evidence_->resolved_directory.data();
  const char *leaf = std::strrchr(path, '/');
  if (!leaf)
    leaf = std::strchr(path, ':');
  const auto created = CreateDir((CONST_STRPTR)(leaf + 1));
  CurrentDir(previous);
  if (!created)
    return std::unexpected{P2_E_IO};
  session->directory_ = DirectoryLock{uintptr_t(created), unlock};
  session->old_directory_ = uintptr_t(CurrentDir(created));
  return session;
}
CardResult<std::array<char, 17>> NativeSession::stamp(Checkpoint point,
                                                      p2_bus &bus) {
  struct DateStamp stamp {};
  DateStamp(&stamp);
  return timeline_.stamp(
      point, Clock{stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick},
      evidence_->offset_east, reference_.data(), bus.now_us(bus.ctx));
}
} // namespace p2::preflight
