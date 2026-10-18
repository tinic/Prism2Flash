/* SPDX-License-Identifier: GPL-2.0-only */
#include "card_profile.h"
#include "compatibility.h"
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
  const Clock now{17822, 720, 0};
  CHECK(std::string_view{clock_label(Clock{}).data()} == "19780101T000000L");
  CHECK(std::string_view{clock_label(Clock{-1, 0, 0}).data()} == "untimed");
  CHECK(std::string_view{clock_label(Clock{0, 1440, 0}).data()} == "untimed");
  CHECK(std::string_view{clock_label(Clock{0, 0, 3000}).data()} == "untimed");
  CHECK(std::string_view{clock_label(Clock{100000, 0, 0}).data()} == "untimed");
  CHECK(std::string_view{clock_label(now).data()} == "20261018T120000L");
  CHECK(std::string_view{clock_label(now, -300).data()} == "20261018T170000Z");
  CHECK(std::string_view{clock_label(Clock{}, 841).data()} == "untimed");
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
  for (auto id : p2::compatibility::PcCards)
    CHECK(p2::compatibility::cis(id >> 16, id & 0xffff));
  CHECK(p2::compatibility::product(
      0, 0, {"Linksys", "Wireless CompactFlash Card", "", ""}));
  CHECK(!p2::compatibility::product(0xffff, 0xffff,
                                    {"Other", "Ethernet", "", ""}));
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
  CHECK(matched->sram_width_inference == 16 && matched->nic.id == 0x800c);
  cis.product = 0x7301;
  CHECK(p2::profile::observe(cis, pda));
  cis.product = 0x7300;
  put(16, 1);
  seal();
  CHECK(p2::profile::observe(cis, pda)->sram_width_inference == 8);
  put(16, 7);
  seal();
  CHECK(!p2::profile::observe(cis, pda));
  put(16, 3);
  put(4, 0x800d);
  seal();
  CHECK(p2::profile::observe(cis, pda));
  for (auto platform : p2::compatibility::Platforms) {
    put(4, platform);
    put(6, 7); // Card revisions and variants are not a one-card tuple lock.
    put(8, 2);
    seal();
    auto observed = p2::profile::observe(cis, pda);
    CHECK(observed && observed->nic.id == platform &&
          observed->nic.variant == 7);
  }
  put(4, 0x8010); // USB-only platform.
  seal();
  CHECK(!p2::profile::observe(cis, pda));
  put(4, 0x800c);
  seal();
  put(14, 5); // RAM-size PDR is valid, and absent configuration uses detection.
  seal();
  auto detected = p2::profile::observe(cis, pda);
  CHECK(detected && detected->ram_size_present &&
        !detected->sram_width_inference);
  put(14, 0x400);
  seal();
  pda[22] ^= 1;
  CHECK(!p2::profile::observe(cis, pda));
  std::puts("Native preflight policy: "
            "best-effort labels and volume/space/profile refusals passed");
}
