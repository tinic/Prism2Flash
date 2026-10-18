/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "p2_stdio.h"
#include "platform.h"
#include "version.h"
#include <optional>
#include <string_view>

/* Retained for AmigaOS Version, including when --version is never called. */
const char p2_version_tag[] __attribute__((used)) =
    "\0$VER: Prism2Genesis " P2_AMIGA_VERSION " (" P2_AMIGA_DATE
    ") " P2_VERSION;

static void usage(void) {
  puts("Prism2Genesis " P2_VERSION " - experimental Genesis RAM probe\n"
       "  Prism2Genesis --version\n"
       "  Prism2Genesis backup --output NEW_DIRECTORY --utc YYYYMMDDTHHMMSSZ\n"
       "  Prism2Genesis info\n"
       "  Prism2Genesis inspect\n"
       "  Prism2Genesis probe --sram 8|16\n"
       "info reads CIS only; inspect initializes the card and reads "
       "firmware/PDA.\n"
       "probe resets the card and writes four RAM bytes, then restores normal "
       "mode.\n"
       "Native inspect/probe are blocked until verified full-flash backup is "
       "available.\n"
       "backup reads all AUX NV addresses twice; it never initializes or "
       "resets the card.\n"
       "Select SRAM width from the exact card's hardware documentation.\n"
       "No firmware loading or flash programming is implemented in this "
       "milestone.");
}
namespace {
struct Options {
  enum class Command { Info, Inspect, Probe } command;
  unsigned bits = 0;
  static std::optional<Options> parse(int argc, char **argv) {
    if (argc == 2 && std::string_view{argv[1]} == "info")
      return Options{Command::Info};
    if (argc == 2 && std::string_view{argv[1]} == "inspect")
      return Options{Command::Inspect};
    if (argc == 4 && std::string_view{argv[1]} == "probe" &&
        std::string_view{argv[2]} == "--sram") {
      const std::string_view width = argv[3];
      if (width == "8" || width == "16")
        return Options{Command::Probe, width == "8" ? 8u : 16u};
    }
    return std::nullopt;
  }
};
void show_id(const char *label, const p2::Identity &id) {
  p2_printf("%s: id=0x%04x variant=%u version=%u.%u\n", label,
            static_cast<unsigned>(id.id), static_cast<unsigned>(id.variant),
            static_cast<unsigned>(id.major), static_cast<unsigned>(id.minor));
}
p2::CardStatus inspect(p2::Card &card, const Options &options) {
  if (auto result = card.initialize(); !result)
    return result;
  auto nic = card.identity(P2_RID_NICID);
  if (!nic)
    return std::unexpected{nic.error()};
  show_id("NIC", *nic);
  if (!p2::Card::supports(nic->id)) {
    p2_fprintf(stderr, "NIC is outside the initial HFA3842 profile list.\n");
    return std::unexpected{P2_E_UNSUPPORTED};
  }
  auto primary = card.identity(P2_RID_PRIID);
  if (!primary)
    return std::unexpected{primary.error()};
  show_id("PRI", *primary);
  if (auto station = card.identity(P2_RID_STAID); station)
    show_id("STA", *station);
  else if (station.error() == P2_E_COMMAND)
    puts("STA: unavailable (primary-only firmware may be running)");
  else
    return std::unexpected{station.error()};
  std::array<uint8_t, P2_PDA_SIZE> pda;
  auto address = card.read_pda(pda);
  if (!address)
    return std::unexpected{address.error()};
  p2_printf("PDA: validated 1024-byte read at 0x%08lx\n",
            static_cast<unsigned long>(*address));
  if (options.command != Options::Command::Probe)
    return {};
  p2_printf("Testing Genesis with %u-bit SRAM; no flash commands.\n",
            options.bits);
  const auto report = card.probe(options.bits);
  p2_printf(
      "COR before=0x%02x CCSR before=0x%02x readback=%02x %02x %02x %02x\n",
      report.cor_before, report.ccsr_before, report.readback[0],
      report.readback[1], report.readback[2], report.readback[3]);
  if (report.cleanup_error)
    p2_fprintf(stderr, "Returning to normal mode: %s\n",
               p2_error_string(report.cleanup_error));
  if (report.error)
    return std::unexpected{report.error};
  // Readback alone does not prove the original firmware restarted.
  if (auto result = card.initialize(); !result)
    return result;
  primary = card.identity(P2_RID_PRIID);
  if (!primary)
    return std::unexpected{primary.error()};
  show_id("PRI after reset", *primary);
  puts("Genesis RAM probe passed; normal firmware responded.");
  return {};
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 2) {
    const std::string_view command = argv[1];
    if (command == "--version") {
      puts("Prism2Genesis " P2_VERSION);
      return 0;
    }
    if (command == "--help" || command == "help") {
      usage();
      return 0;
    }
  }
  if (argc == 6 && std::string_view{argv[1]} == "backup" &&
      std::string_view{argv[2]} == "--output" &&
      std::string_view{argv[4]} == "--utc")
    return p2::backup::run_command(argv[3], argv[5]);
  const auto options = Options::parse(argc, argv);
  if (!options) {
    usage();
    return 20;
  }
#if defined(PRISM2_AMIGA) || defined(PRISM2_REQUIRE_BACKUP)
  // Refuse before opening the slot. An existing file or force option must
  // never bypass the unimplemented physical-flash backup approval gate.
  if (options->command != Options::Command::Info) {
    p2_fprintf(stderr, "Operation blocked: a verified timestamped full-flash "
                       "backup is required.\n"
                       "Full-flash backup is not implemented; no card access "
                       "was attempted.\n");
    return 20;
  }
#endif
  p2_printf("Prism2Genesis " P2_VERSION " - %s\n", p2_platform_description());
  auto slot = p2::Slot::open();
  if (!slot) {
    p2_fprintf(stderr, "%s\n", slot.error().message.data());
    return 20;
  }
  const auto &info = slot->info();
  p2_printf("PCMCIA: manufacturer=0x%04x product=0x%04x\n",
            static_cast<unsigned>(info.manufacturer),
            static_cast<unsigned>(info.product));
  if (options->command == Options::Command::Info)
    return 0;
  auto bus = slot->configure();
  if (!bus) {
    p2_fprintf(stderr, "%s\n", bus.error().message.data());
    return 20;
  }
  p2::Card card{*bus};
  if (auto result = inspect(card, *options); !result) {
    p2_fprintf(stderr, "Operation failed: %s\n",
               p2_error_string(result.error()));
    return 20;
  }
  return 0;
}
