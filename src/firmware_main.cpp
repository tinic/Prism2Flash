/* SPDX-License-Identifier: GPL-2.0-only */
#include "file.h"
#include "firmware.h"
#include "p2_stdio.h"
#include "sha256.h"
#include "version.h"
#include <optional>
#include <stdlib.h>
#include <string_view>

const char p2_firmware_version_tag[] __attribute__((used)) =
    "\0$VER: Prism2Firmware " P2_AMIGA_VERSION " (" P2_AMIGA_DATE
    ") " P2_VERSION;
static void usage(void) {
  puts("Prism2Firmware " P2_VERSION
       " - offline firmware inspection and validation\n"
       "  Prism2Firmware --version\n"
       "  Prism2Firmware inspect IMAGE\n"
       "  Prism2Firmware validate IMAGE --mode ram|flash --card SNAPSHOT --pda "
       "PDA\n"
       "                         [--primary PRIMARY_IMAGE]\n"
       "  Prism2Firmware prepare IMAGE --mode ram|flash --card SNAPSHOT --pda "
       "PDA\n"
       "                        [--primary PRIMARY_IMAGE]\n"
       "PDA must be a CRC-checked 1024-byte binary backup. See "
       "docs/OFFLINE-FIRMWARE.md.\n"
       "prepare builds a private in-memory copy and reports its hashes.\n"
       "This utility reads files only. It does not access cards, export "
       "prepared "
       "payloads or write firmware.");
}
namespace {
using namespace p2::firmware;
struct Options {
  enum class Command { Inspect, Validate, Prepare } command;
  const char *image;
  std::optional<Mode> mode;
  const char *card = nullptr;
  const char *pda = nullptr;
  const char *primary = nullptr;

  static std::optional<Options> parse(int argc, char **argv) {
    if (argc < 3)
      return std::nullopt;
    const std::string_view command = argv[1];
    if (command != "inspect" && command != "validate" && command != "prepare")
      return std::nullopt;
    Options options{command == "inspect"    ? Command::Inspect
                    : command == "validate" ? Command::Validate
                                            : Command::Prepare,
                    argv[2], std::nullopt};
    if (options.command == Command::Inspect)
      return argc == 3 ? std::optional{options} : std::nullopt;
    for (int i = 3; i < argc; i += 2) {
      if (i + 1 == argc)
        return std::nullopt;
      const std::string_view key = argv[i], value = argv[i + 1];
      if (key == "--mode" && !options.mode) {
        if (value != "ram" && value != "flash")
          return std::nullopt;
        options.mode = value == "ram" ? Mode::Ram : Mode::Flash;
      } else if (key == "--card" && !options.card)
        options.card = argv[i + 1];
      else if (key == "--pda" && !options.pda)
        options.pda = argv[i + 1];
      else if (key == "--primary" && !options.primary)
        options.primary = argv[i + 1];
      else
        return std::nullopt;
    }
    if (!options.mode || !options.card || !options.pda)
      return std::nullopt;
    return options;
  }
};
void show(const Image &image) {
  p2_printf("Source SHA-256: %s\n", image.source_sha256().data());
  const auto &id = image.component();
  p2_printf("Component: id=0x%04x major=%u minor=%u variant=%u\n", id.id,
            id.major, id.minor, id.variant);
  p2_printf("Class: %s; entry=0x%08lx (separate from S3 load addresses)\n",
            image.entry() ? "RAM" : "flash",
            static_cast<unsigned long>(image.entry()));
  p2_printf("Payload: %lu bytes in %lu segments; %lu S3 records\n",
            static_cast<unsigned long>(image.payload_bytes()),
            static_cast<unsigned long>(image.segments().size()),
            image.records());
  if (!image.name().empty())
    p2_printf("Name: %s\n", image.name().data());
  if (auto sequence = image.build_sequence())
    p2_printf("Build sequence: %u\n", *sequence);
  for (const auto &segment : image.segments())
    p2_printf("Segment: address=0x%08lx bytes=%lu\n",
              static_cast<unsigned long>(segment.address),
              static_cast<unsigned long>(segment.data.size()));
  for (const auto &platform : image.platforms())
    p2_printf("Platform: id=0x%04x major=%u minor=%u variant=%u\n", platform.id,
              platform.major, platform.minor, platform.variant);
  for (const auto &range : image.ranges())
    p2_printf("Interface: %s id=%u variant=%u bottom=%u top=%u\n",
              range.role ? "actor" : "supplier", range.interface_id,
              range.variant, range.bottom, range.top);
  for (const auto &plug : image.plugs())
    p2_printf("Plug: pdr=0x%08lx address=0x%08lx bytes=%lu\n",
              static_cast<unsigned long>(plug.pdr),
              static_cast<unsigned long>(plug.address),
              static_cast<unsigned long>(plug.length));
  for (const auto &signature : image.signatures())
    p2_printf("Signature descriptor: start=0x%08lx bytes=%lu program=%lu\n",
              static_cast<unsigned long>(signature.start),
              static_cast<unsigned long>(signature.length),
              static_cast<unsigned long>(signature.program));
  puts("Signature descriptors are not computed CRC verification. Firmware "
       "bytes are unchanged.");
}
Result<Image> load(const char *path) {
  auto file = p2::File::open(path, "rb");
  if (!file) {
    p2_fprintf(stderr, "Cannot open image: %s\n", path);
    return std::unexpected{Error{Errc::Io, 0, "Cannot open image"}};
  }
  auto image = Image::read(*file);
  if (!image)
    p2_fprintf(stderr, "Image rejected: %s (line %lu): %s\n", path,
               image.error().line, image.error().message);
  return image;
}
int validate(const Options &options, const Image &image) {
  auto snapshot_file = p2::File::open(options.card, "rb");
  if (!snapshot_file) {
    p2_fprintf(stderr, "Cannot open card snapshot: %s\n", options.card);
    return 20;
  }
  auto card = CardSnapshot::read(*snapshot_file);
  if (!card) {
    p2_fprintf(stderr, "Card snapshot rejected (line %lu): %s\n",
               card.error().line, card.error().message);
    return 20;
  }
  auto pda = p2::ByteBuffer::allocate(P2_PDA_SIZE);
  if (!pda) {
    p2_fprintf(stderr, "Out of memory for PDA\n");
    return 20;
  }
  auto pda_file = p2::File::open(options.pda, "rb");
  if (!pda_file) {
    p2_fprintf(stderr, "Cannot open PDA backup: %s\n", options.pda);
    return 20;
  }
  if (fread(pda.bytes().data(), 1, pda.size(), pda_file.get()) != pda.size() ||
      fgetc(pda_file.get()) != EOF || ferror(pda_file.get())) {
    p2_fprintf(stderr, "PDA backup must contain exactly 1024 bytes\n");
    return 20;
  }
  std::optional<Image> primary;
  if (options.primary) {
    auto parsed = load(options.primary);
    if (!parsed)
      return 20;
    primary = std::move(*parsed);
  }
  if (auto result = image.validate(*options.mode, *card, pda.bytes(),
                                   primary ? &*primary : nullptr);
      !result) {
    p2_fprintf(stderr, "Offline validation rejected: %s\n",
               result.error().message);
    return 20;
  }
  if (options.command == Options::Command::Prepare) {
    const auto basename = [](std::string_view path) {
      const auto separator = path.find_last_of("/\\:");
      if (separator != path.npos)
        path.remove_prefix(separator + 1);
      return path;
    };
    std::optional<ImageSource> source;
    if (primary)
      source.emplace(*primary, basename(options.primary));
    auto prepared =
        PreparedImage::create({image, basename(options.image)}, *options.mode,
                              *card, pda.bytes(), source);
    if (!prepared) {
      p2_fprintf(stderr, "Offline preparation rejected: %s\n",
                 prepared.error().message);
      return 20;
    }
    p2_printf("Source SHA-256: %s\n", image.source_sha256().data());
    if (primary)
      p2_printf("Primary source SHA-256: %s\n",
                primary->source_sha256().data());
    p2::Sha256 pda_hash;
    pda_hash.update(pda.bytes());
    p2_printf("PDA SHA-256: %s\n", pda_hash.hex().data());
    p2_printf("Prepared: %lu bytes in %lu segments; entry=0x%08lx\n",
              static_cast<unsigned long>(prepared->payload_bytes()),
              static_cast<unsigned long>(prepared->segments().size()),
              static_cast<unsigned long>(prepared->entry()));
    for (const auto &segment : prepared->segments()) {
      p2::Sha256 hash;
      hash.update(segment.data.bytes());
      p2_printf("Prepared segment: address=0x%08lx bytes=%lu sha256=%s\n",
                static_cast<unsigned long>(segment.address),
                static_cast<unsigned long>(segment.data.size()),
                hash.hex().data());
    }
    puts("Offline preparation passed. PDA plugs, filename fields and C0DE "
         "stamps\n"
         "were applied to private copies. Source/PDA files are unchanged.\n"
         "No payload was exported. Hardware profile, full-backup operation "
         "gate\n"
         "and card programming are not approved by this report.");
    return 0;
  }
  puts("Offline metadata and PDA checks passed. Firmware bytes are unchanged.\n"
       "Hardware profile/address-map approval and prepare/merge are still "
       "required.");
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 2) {
    const std::string_view command = argv[1];
    if (command == "--version") {
      puts("Prism2Firmware " P2_VERSION);
      return 0;
    }
    if (command == "--help" || command == "help") {
      usage();
      return 0;
    }
  }
  const auto options = Options::parse(argc, argv);
  if (!options) {
    usage();
    return 20;
  }
  auto image = load(options->image);
  if (!image)
    return 20;
  if (options->command != Options::Command::Inspect)
    return validate(*options, *image);
  show(*image);
  puts("Structural inspection passed; card compatibility is not established.");
  return 0;
}
