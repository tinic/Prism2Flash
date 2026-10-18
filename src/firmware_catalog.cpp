/* SPDX-License-Identifier: GPL-2.0-only */
#include "firmware_catalog.h"
#include "file.h"
#include <tuple>
namespace p2::firmware {
namespace {
auto version(const Image &image) {
  const auto &id = image.component();
  return std::tuple{id.major, id.minor, id.variant};
}
std::optional<Image> read(std::string_view path) {
  auto file = File::open(path.data(), "rb");
  if (!file)
    return {};
  auto image = Image::read(*file);
  if (!image || !file.close())
    return {};
  return std::move(*image);
}
} // namespace
Result<Selection> select(std::span<const std::string_view> paths,
                         const CardSnapshot &card,
                         std::span<const uint8_t> pda) {
  std::optional<Selection> best;
  for (size_t p = 0; p < paths.size(); ++p) {
    auto primary = read(paths[p]);
    if (!primary || primary->component().id != 0x15 ||
        !primary->validate(Mode::Flash, card, pda, nullptr,
                           PlugPolicy::DosOptionalDefaults))
      continue;
    for (size_t s = 0; s < paths.size(); ++s) {
      auto station = read(paths[s]);
      if (!station || station->component().id != 0x1f ||
          !station->validate(Mode::Flash, card, pda, &*primary,
                             PlugPolicy::DosOptionalDefaults))
        continue;
      if (best && (version(*station) < version(best->station) ||
                   (version(*station) == version(best->station) &&
                    version(*primary) <= version(best->primary))))
        continue;
      // Keep primary available for the remaining pairs; reopen only when
      // selected.
      auto selected_primary = read(paths[p]);
      if (!selected_primary)
        return std::unexpected{
            Error{Errc::Format, 0, "Selected primary file changed"}};
      best.emplace(
          Selection{std::move(*selected_primary), std::move(*station), p, s});
    }
  }
  if (!best)
    return std::unexpected{
        Error{Errc::Incompatible, 0,
              "Firmware drawer contains no compatible PRI/STA pair"}};
  return std::move(*best);
}
std::string_view initial_codes(uint16_t nic, unsigned bits) {
  if (bits != 8 && bits != 16)
    return {};
  switch (nic) {
  case 0x8000:
  case 0x8009:
    return "G";
  case 0x8002:
  case 0x8003:
    return "1H";
  case 0x8008:
    return "4";
  case 0x800b:
  case 0x8012:
  case 0x8016:
  case 0x801a:
    return bits == 16 ? "BD"
                      : "A"; // DOS prismdos.txt also uses ID on WPC11 v3.
  case 0x800c:
  case 0x8013:
  case 0x8017:
  case 0x801b:
  case 0x8022:
  case 0x8023:
    return bits == 16 ? "D" : "C";
  case 0x800d:
  case 0x8014:
  case 0x8018:
  case 0x801c:
    return "E";
  default:
    return {};
  }
}
} // namespace p2::firmware
