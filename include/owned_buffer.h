/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_OWNED_BUFFER_H
#define P2_OWNED_BUFFER_H

#include <cstdint>
#include <cstdlib>
#include <memory>

namespace p2 {
struct FreeMemory {
  void operator()(void *memory) const noexcept { std::free(memory); }
};

using OwnedBuffer = std::unique_ptr<uint8_t[], FreeMemory>;

// Keep explicit allocation-failure handling on AmigaOS; no throwing new.
inline OwnedBuffer allocate_bytes(size_t bytes) {
  return OwnedBuffer{static_cast<uint8_t *>(std::malloc(bytes))};
}
} // namespace p2

#endif
