/* SPDX-License-Identifier: GPL-2.0-only */
// Host-only byte oracle adapter. No transport or backend is linked.
#include "file.h"
#include "firmware.h"
#include "flash_plan.h"
#include "sha256.h"
#include <cassert>

using namespace p2::firmware;
static Result<Image> load(const char *name) {
  auto file = p2::File::open(name, "rb");
  assert(file);
  return Image::read(*file);
}
static auto payload_hash(const Image &image) {
  p2::Sha256 hash;
  for (const auto &segment : image.segments())
    hash.update(segment.data.bytes());
  return hash.hex();
}
int main(int argc, char **argv) {
  assert(argc == 4 || argc == 5);
  auto image = load(argv[1]);
  assert(image);
  const auto before = payload_hash(*image);
  auto pda = p2::File::open(argv[2], "rb");
  auto snapshot = p2::File::open(argv[3], "rb");
  assert(pda && snapshot);
  std::array<uint8_t, P2_PDA_SIZE> bytes;
  assert(fread(bytes.data(), 1, bytes.size(), pda.get()) == bytes.size());
  const auto original_pda = bytes;
  auto card = CardSnapshot::read(*snapshot);
  assert(card);
  const auto basename = [](std::string_view name) {
    return name.substr(name.find_last_of('/') + 1);
  };
  std::optional<Image> primary;
  std::optional<ImageSource> source;
  std::array<char, 65> primary_before{};
  if (argc == 5) {
    auto parsed = load(argv[4]);
    assert(parsed);
    primary.emplace(std::move(*parsed));
    primary_before = payload_hash(*primary);
    source.emplace(*primary, basename(argv[4]));
  }
  auto prepared = PreparedImage::create(
      {*image, basename(argv[1])}, image->entry() ? Mode::Ram : Mode::Flash,
      *card, bytes, source);
  assert(prepared);
  assert(before == payload_hash(*image) && original_pda == bytes);
  assert(!primary || primary_before == payload_hash(*primary));
  if (!image->entry()) {
    auto before_flash = p2::ByteBuffer::allocate(p2::flash::Bytes);
    assert(before_flash);
    for (std::size_t i = 0; i < before_flash.size(); ++i)
      before_flash.bytes()[i] = static_cast<uint8_t>(i * 37);
    auto plan = p2::flash::Plan::create(before_flash.bytes(), *prepared);
    assert(plan);
    for (std::size_t offset = 0; offset < p2::flash::Bytes; ++offset) {
      uint8_t expected = before_flash.bytes()[offset];
      for (const auto &segment : prepared->segments())
        if (segment.contains(0x7e0000 + offset, 1))
          expected = segment.data.bytes()[offset + 0x7e0000 - segment.address];
      assert(plan->desired()[offset] == expected);
      assert(plan->before()[offset] == before_flash.bytes()[offset]);
    }
  }
  puts("CANONICAL");
  printf("I %lu\n", static_cast<unsigned long>(prepared->entry()));
  for (const auto &segment : prepared->segments()) {
    printf("S %lu %lu ", static_cast<unsigned long>(segment.address),
           static_cast<unsigned long>(segment.data.size()));
    for (const auto byte : segment.data.bytes())
      printf("%02x", byte);
    puts("");
  }
}
