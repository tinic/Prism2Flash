/* SPDX-License-Identifier: GPL-2.0-only
 * Bare-drive environment for native executable smoke tests.
 */
#include "owned_buffer.h"
#include "sha256.h"
#include "storage.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <dos/dos.h>
#include <proto/dos.h>
#include <stddef.h>
#include <string.h>

static bool check_allocator() {
  struct Allocation {
    p2::OwnedBuffer bytes;
    size_t size = 0;
    uint8_t value = 0;
  };
  std::array<Allocation, 16> allocations{};
  for (unsigned round = 0; round < 1024; ++round) {
    auto &slot = allocations[round % allocations.size()];
    for (size_t i = 0; i < slot.size; ++i)
      if (slot.bytes[i] != slot.value)
        return false;
    const size_t next_size = (round * 47 + 11) % 4096 + 1;
    auto *next =
        static_cast<uint8_t *>(std::realloc(slot.bytes.get(), next_size));
    if (!next)
      return false;
    slot.bytes.release();
    slot.bytes.reset(next);
    if (reinterpret_cast<uintptr_t>(next) % alignof(std::max_align_t))
      return false;
    for (size_t i = 0; i < std::min(slot.size, next_size); ++i)
      if (next[i] != slot.value)
        return false;
    slot.size = next_size;
    slot.value = static_cast<uint8_t>(round);
    memset(next, slot.value, next_size);
    if (round % 3 == 0) {
      slot.bytes.reset();
      slot.size = 0;
    }
  }
  return true;
}

// Exercise the actual firmware storage types on the native runtime as well.
static bool check_storage() {
  p2::BoundedVector<p2::ByteBuffer, 32> records;
  for (unsigned i = 0; i < 32; ++i) {
    auto bytes = p2::ByteBuffer::allocate(i + 1);
    if (!bytes)
      return false;
    std::fill(bytes.bytes().begin(), bytes.bytes().end(),
              static_cast<uint8_t>(i));
    if (!records.push_back(std::move(bytes)))
      return false;
  }
  auto moved = std::move(records);
  if (!records.empty() || moved.size() != 32)
    return false;
  moved.truncate(17);
  for (unsigned i = 0; i < moved.size(); ++i)
    for (auto byte : moved[i].bytes())
      if (byte != i)
        return false;
  return true;
}

int main(int argc, char **argv) {
  if (!check_allocator() || !check_storage())
    return RETURN_FAIL;
  static const char *const names[] = {"ENV",   "ENVARC", "T",
                                      "CLIPS", "LIBS",   "DEVS"};
  static const char *const paths[] = {"DH0:env",   "DH0:envarc", "DH0:t",
                                      "DH0:clips", "DH0:libs",   "DH0:devs"};
  unsigned i;
  p2::Sha256 hash;
  hash.update("abc");
  const auto digest = hash.hex();
  if (strcmp(
          digest.data(),
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
    return RETURN_FAIL;
  if (argc == 2 && !strcmp(argv[1], "--runtime"))
    return RETURN_OK;
  for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    BPTR lock = Lock((CONST_STRPTR)paths[i], SHARED_LOCK);
    if (!lock)
      return RETURN_FAIL;
    if (!AssignLock((CONST_STRPTR)names[i], lock)) {
      UnLock(lock);
      return RETURN_FAIL;
    }
  }
  return RETURN_OK;
}
