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
                    version(*primary) <= version(*best->primary))))
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
  if (!best && card.suppliers[2]) {
    for (size_t s = 0; s < paths.size(); ++s) {
      auto station = read(paths[s]);
      if (!station || station->component().id != 0x1f ||
          !station->validate(Mode::Flash, card, pda, nullptr,
                             PlugPolicy::DosOptionalDefaults))
        continue;
      if (!best || version(*station) > version(best->station))
        best.emplace(
            Selection{std::nullopt, std::move(*station), paths.size(), s});
    }
  }
  if (!best)
    return std::unexpected{
        Error{Errc::Incompatible, 0,
              "Firmware drawer contains no compatible PRI/STA pair"}};
  return std::move(*best);
}
} // namespace p2::firmware
