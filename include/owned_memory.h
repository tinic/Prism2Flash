/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>

namespace p2 {
// Amiga's C allocator is already checked by the toolchain/runtime gates. Using
// it underneath typed ownership avoids pulling libstdc++'s throwing new/EH
// startup into exception-free executables for Kickstart 2.x.
struct Heap {
  static void *allocate(std::size_t size) noexcept { return std::malloc(size); }
  static void deallocate(void *pointer) noexcept { std::free(pointer); }
};
template <typename T> struct DestroyObject {
  void operator()(T *object) const noexcept {
    std::destroy_at(object);
    Heap::deallocate(object);
  }
};
template <typename T> using Owned = std::unique_ptr<T, DestroyObject<T>>;
template <typename T> [[nodiscard]] Owned<T> make_owned() {
  static_assert(std::is_nothrow_default_constructible_v<T>);
  static_assert(alignof(T) <= alignof(std::max_align_t));
  auto *storage = static_cast<T *>(Heap::allocate(sizeof(T)));
  if (!storage)
    return {};
  return Owned<T>{std::construct_at(storage)};
}
template <typename T, typename Allocator = Heap> struct DestroyArray {
  std::size_t size = 0;
  void operator()(T *objects) const noexcept {
    std::destroy_n(objects, size);
    Allocator::deallocate(objects);
  }
};
template <typename T, typename Allocator = Heap>
using OwnedArray = std::unique_ptr<T[], DestroyArray<T, Allocator>>;
template <typename T, typename Allocator = Heap>
[[nodiscard]] OwnedArray<T, Allocator> allocate_array(std::size_t size) {
  static_assert(std::is_nothrow_default_constructible_v<T>);
  static_assert(alignof(T) <= alignof(std::max_align_t));
  if (size > std::numeric_limits<std::size_t>::max() / sizeof(T))
    return {};
  auto *storage = static_cast<T *>(Allocator::allocate(size * sizeof(T)));
  if (!storage)
    return {};
  for (std::size_t i = 0; i < size; ++i)
    std::construct_at(storage + i);
  return OwnedArray<T, Allocator>{storage, DestroyArray<T, Allocator>{size}};
}
} // namespace p2
