/* SPDX-License-Identifier: GPL-2.0-only */
#include "card_profile.h"
#include "preflight.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                          \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
// Pure policy test must never resolve a real path or talk to a backend.
p2::CardResult<p2::Owned<p2::preflight::Evidence>>
p2::preflight::native(const char *, const char *, int) {
  std::abort();
}
int main() {
  using namespace p2::preflight;
  CHECK(!utc_seconds("20260229T000000Z"));
  auto reference = utc_seconds("20261018T120000Z");
  CHECK(reference);
  CHECK(*reference == 1539864000ll);
  auto stamp = [&](int64_t seconds) {
    return Clock{int32_t(seconds / 86400), int32_t(seconds % 86400 / 60),
                 int32_t(seconds % 60 * 50)};
  };
  CHECK(check_clock(stamp(*reference), 0, "20261018T120000Z"));
  CHECK(check_clock(stamp(*reference - 420 * 60), -420, "20261018T120000Z"));
  CHECK(check_clock(stamp(*reference + 330 * 60), 330, "20261018T120000Z"));
  for (int delta : {-121, 121, 86400})
    CHECK(!check_clock(stamp(*reference + delta), 0, "20261018T120000Z"));
  for (int delta : {-120, 120})
    CHECK(check_clock(stamp(*reference + delta), 0, "20261018T120000Z"));
  // A several-minute capture advances past the initial reference's skew
  // window, including a wrapping 32-bit timer. Wall-clock jumps still fail.
  Timeline timeline;
  constexpr uint32_t timer_start = 0xffff0000;
  auto boundary = [&](Checkpoint point, unsigned seconds) {
    return timeline.stamp(point, stamp(*reference + seconds - 300 * 60), -300,
                          "20261018T120000Z", timer_start + seconds * 1000000u);
  };
  auto first = boundary(Checkpoint::BeforeStart, 0);
  CHECK(first && std::string_view{first->data()} == "20261018T120000Z");
  CHECK(boundary(Checkpoint::BeforeEnd, 183));
  CHECK(boundary(Checkpoint::AfterStart, 184));
  auto last = boundary(Checkpoint::AfterEnd, 367);
  CHECK(last && std::string_view{last->data()} == "20261018T120607Z");
  CHECK(!boundary(Checkpoint::AfterEnd, 368));
  for (int delta : {-60, 60, 3600}) {
    Timeline jumping;
    CHECK(jumping.stamp(Checkpoint::BeforeStart, stamp(*reference), 0,
                        "20261018T120000Z", 0));
    CHECK(!jumping.stamp(Checkpoint::BeforeEnd, stamp(*reference + 183 + delta),
                         0, "20261018T120000Z", 183000000));
    CHECK(!jumping.stamp(Checkpoint::BeforeEnd, stamp(*reference + 183), 0,
                         "20261018T120000Z", 183000000));
  }
  Timeline reversed;
  CHECK(!reversed.stamp(Checkpoint::AfterStart, stamp(*reference), 0,
                        "20261018T120000Z", 0));
  Timeline expired;
  CHECK(expired.stamp(Checkpoint::BeforeStart, stamp(*reference), 0,
                      "20261018T120000Z", 0));
  CHECK(!expired.stamp(Checkpoint::BeforeEnd, stamp(*reference + 1801), 0,
                       "20261018T120000Z", 1801000000));
  static unsigned released = 0;
  auto release_lock = +[](uintptr_t value) { released += unsigned(value); };
  {
    DirectoryLock first_lock{1, release_lock};
    DirectoryLock second_lock{2, release_lock};
    auto moved_lock = std::move(first_lock);
    CHECK(!first_lock.get() && moved_lock.get() == 1);
    second_lock = std::move(moved_lock);
    CHECK(released == 2 && !moved_lock.get() && second_lock.get() == 1);
  }
  CHECK(released == 3);
  CHECK(!check_clock(Clock{-1, 0, 0}, 0, "20261018T120000Z"));
  CHECK(!check_clock(Clock{17822, 1440, 0}, 0, "20261018T120000Z"));
  CHECK(!check_clock(Clock{17822, 0, 3000}, 0, "20261018T120000Z"));
  CHECK(!check_clock(stamp(*reference), 841, "20261018T120000Z"));
  auto leap = utc_seconds("20240229T000000Z"),
       march = utc_seconds("20240301T000000Z");
  CHECK(leap && march && *march - *leap == 86400);
  Volume good{0x444f5301, 82, 1000000, 0, 512, false};
  CHECK(check_volume(good));
  for (auto bad : {Volume{0x52414d00, 82, 1000000, 0, 512, false},
                   Volume{0x444f5301, 82, 1000000, 0, 512, true},
                   Volume{0x444f5301, 80, 1000000, 0, 512, false},
                   Volume{0x444f5301, 82, 100, 0, 512, false},
                   Volume{0x444f5301, 82, 100, 101, 512, false},
                   Volume{0x444f5301, 82, 100, -1, 512, false},
                   Volume{0x444f5301, 82, 1000000, 0, -1, false}})
    CHECK(!check_volume(bad));
  CHECK(p2::profile::programming_approved());
  // Original synthetic PDA with independent polynomial-long-division CRC.
  std::array<uint8_t, P2_PDA_SIZE> pda{};
  auto put = [&](size_t pos, uint16_t value) {
    pda[pos] = value;
    pda[pos + 1] = value >> 8;
  };
  put(0, 5);
  put(2, 8);
  put(4, 0x800c);
  put(6, 0);
  put(8, 1);
  put(10, 0);
  put(12, 2);
  put(14, 0x400);
  put(16, 3);
  put(18, 2);
  put(20, 0);
  auto seal = [&] {
    uint16_t crc = 0;
    for (size_t i = 0; i < 22; ++i) {
      unsigned reversed = 0;
      for (unsigned bit = 0; bit < 8; ++bit)
        reversed |= ((pda[i] >> bit) & 1) << (7 - bit);
      crc ^= reversed << 8;
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0));
    }
    uint16_t reversed = 0;
    for (unsigned bit = 0; bit < 16; ++bit)
      reversed |= ((crc >> bit) & 1) << (15 - bit);
    put(22, reversed);
  };
  seal();
  p2_card_info cis{0xb, 0x7300, 0, 0, 0};
  auto matched = p2::profile::observe(cis, pda);
  CHECK(matched);
  CHECK(
      matched->sram_width_inference == 16 && !matched->hardware_authorization &&
      !matched->physical_coverage_verified && !matched->sram_capacity_verified);
  cis.product = 0x7301;
  CHECK(!p2::profile::observe(cis, pda));
  cis.product = 0x7300;
  put(16, 1);
  seal();
  CHECK(!p2::profile::observe(cis, pda));
  put(16, 3);
  put(4, 0x800d);
  seal();
  CHECK(!p2::profile::observe(cis, pda));
  put(4, 0x800c);
  seal();
  pda[22] ^= 1;
  CHECK(!p2::profile::observe(cis, pda));
  std::puts("Native preflight policy: "
            "clock/skew/offset/calendar/volume/space/profile refusals passed");
}
