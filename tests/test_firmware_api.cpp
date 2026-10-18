/* SPDX-License-Identifier: GPL-2.0-only */
#include "file.h"
#include "firmware.h"
#include <algorithm>
#include <cassert>
#include <type_traits>

using namespace p2::firmware;
static const uint8_t input[] = "S307007E0002030471\n"
                               "S30AFF2000000500010015BB\n"
                               "S30CFF20000500020003000400C6\n"
                               "S705003F0000BB\n"
                               "S307007E0000010277\n";
static_assert(!std::is_copy_constructible_v<Image>);
static_assert(std::is_nothrow_move_constructible_v<Image>);
// Inject failures at the storage allocator; keep sanitizer allocation checks.
struct Tracked {
  static inline unsigned owned = 0;
  unsigned value = 0;
  Tracked() noexcept = default;
  explicit Tracked(unsigned v) noexcept : value(v) { ++owned; }
  Tracked(Tracked &&other) noexcept : value(std::exchange(other.value, 0)) {}
  Tracked &operator=(Tracked &&other) noexcept {
    if (value)
      --owned;
    value = std::exchange(other.value, 0);
    return *this;
  }
  ~Tracked() {
    if (value)
      --owned;
  }
};
struct FaultHeap {
  static inline bool fail_allocation = false;
  static void *allocate(std::size_t bytes) noexcept {
    return fail_allocation ? nullptr : p2::Heap::allocate(bytes);
  }
  static void deallocate(void *p) noexcept { p2::Heap::deallocate(p); }
};
static void storage_failures() {
  {
    p2::BoundedVector<Tracked, 32, FaultHeap> items;
    FaultHeap::fail_allocation = true;
    assert(!items.push_back(Tracked{1}));
    assert(items.empty() && !Tracked::owned);
    FaultHeap::fail_allocation = false;
    for (unsigned i = 1; i <= 16; ++i)
      assert(items.push_back(Tracked{i}));
    FaultHeap::fail_allocation = true;
    assert(!items.push_back(Tracked{17}));
    assert(items.size() == 16 && Tracked::owned == 16);
    for (unsigned i = 0; i < items.size(); ++i)
      assert(items[i].value == i + 1);
    FaultHeap::fail_allocation = false;
    assert(items.push_back(Tracked{17}));
    auto moved = std::move(items);
    assert(items.empty() && moved.size() == 17 && Tracked::owned == 17);
    moved.truncate(5);
    assert(Tracked::owned == 5);
    items = std::move(moved);
    assert(moved.empty() && items.size() == 5 && Tracked::owned == 5);
  }
  assert(Tracked::owned == 0);
}
int main() {
  storage_failures();
  const std::array<uint8_t, 4> expected{1, 2, 3, 4};
  const uint8_t invalid_pda[P2_PDA_SIZE] = {1, 0, 0, 0, 2, 0, 0, 0, 201, 184};
  auto parsed = Image::parse(std::span{input}.first(sizeof(input) - 1));
  assert(parsed && parsed->entry() == 0x3f0000 &&
         parsed->segments().size() == 1);
  assert(parsed->component().id == 0x15 && parsed->component().variant == 2);
  assert(parsed->component().major == 3 && parsed->component().minor == 4);
  // Moving an image transfers the combined payload; borrowed views stay valid.
  const auto bytes = parsed->segments()[0].data.bytes();
  auto image = std::move(*parsed);
  assert(image.segments()[0].address == 0x7e0000);
  assert(
      std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()));
  assert(bytes.data() == image.segments()[0].data.bytes().data());
  assert(image.contains(0x7e0000, 4) && !image.contains(0x7e0001, 4));
  assert(!image.validate(Mode::Ram, CardSnapshot{}, invalid_pda));
  const auto empty = Image::parse({});
  assert(!empty && empty.error().code == Errc::Format &&
         empty.error().message[0]);
  assert(p2_validate_pda(invalid_pda) == P2_E_PDA);
  return 0;
}
