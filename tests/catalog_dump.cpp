/* SPDX-License-Identifier: GPL-2.0-only */
#include "file.h"
#include "firmware_catalog.h"
#include <cassert>
#include <cstdio>
int main(int argc, char **argv) {
  using namespace p2;
  assert(firmware::initial_codes(0x800c, 16) == "D");
  assert(firmware::initial_codes(0x800c, 8) == "C");
  assert(firmware::initial_codes(0x801a, 16) == "BD");
  assert(firmware::initial_codes(0x8008, 8) == "4");
  assert(firmware::initial_codes(0x8010, 16).empty());
  if (argc < 3 || argc > 130)
    return 20;
  auto file = File::open(argv[1], "rb");
  std::array<uint8_t, P2_PDA_SIZE> pda{};
  if (!file || fread(pda.data(), 1, pda.size(), file.get()) != pda.size() ||
      !file.close())
    return 20;
  auto card = firmware::CardSnapshot::from_pda(pda);
  if (!card)
    return 20;
  std::array<std::string_view, 128> paths{};
  for (int i = 2; i < argc; ++i)
    paths[i - 2] = argv[i];
  auto chosen = firmware::select(std::span{paths}.first(argc - 2), *card, pda);
  if (!chosen)
    return 20;
  printf("%zu %zu\n", chosen->primary_index, chosen->station_index);
}
