/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "operation.h"
#include "p2_stdio.h"
#include "platform.h"
#ifdef PRISM2_AMIGA
#include "preflight.h"
#endif
#include "version.h"
#include <charconv>
#include <cstring>
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
       "  Prism2Genesis preflight --output NEW_DIRECTORY --utc "
       "YYYYMMDDTHHMMSSZ\n"
       "    --utc-offset MINUTES_EAST_OF_UTC (native; filesystem/clock only)\n"
       "  Prism2Genesis info\n"
       "  Prism2Genesis inspect\n"
       "  Prism2Genesis probe --sram 8|16\n"
       "Host model transaction: append --output NEW_DIRECTORY --utc "
       "YYYYMMDDTHHMMSSZ\n"
       "to inspect/probe to save and verify fresh before/after NV captures.\n"
       "info reads CIS only; inspect initializes the card and reads "
       "firmware/PDA.\n"
       "probe resets the card and writes four RAM bytes, then restores normal "
       "mode.\n"
       "Native MA401 x16 probe: append --output NEW_DIRECTORY --utc "
       "YYYYMMDDTHHMMSSZ\n"
       "  --utc-offset MINUTES_EAST_OF_UTC; fresh before/after backups are "
       "mandatory.\n"
       "Native inspect, x8 probing and probing without backup arguments "
       "remain blocked.\n"
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
  const char *directory = nullptr, *utc = nullptr;
  int offset_east = 10000;
  bool native_profile = false;
  static std::optional<Options> parse(int argc, char **argv) {
    const char *directory = nullptr, *utc = nullptr;
    int east = 10000;
    if (argc >= 4 && std::string_view{argv[argc - 2]} == "--utc-offset") {
      const char *text = argv[argc - 1];
      const char *end = text + std::strlen(text);
      auto parsed = std::from_chars(text, end, east);
      if (parsed.ec != std::errc{} || parsed.ptr != end || east < -840 ||
          east > 840)
        return std::nullopt;
      argc -= 2;
    }
    if (argc >= 6 && std::string_view{argv[argc - 4]} == "--output" &&
        std::string_view{argv[argc - 2]} == "--utc") {
      directory = argv[argc - 3];
      utc = argv[argc - 1];
      argc -= 4;
    }
    if (argc == 2 && std::string_view{argv[1]} == "info")
      return directory || east != 10000 ? std::nullopt
                                        : std::optional{Options{Command::Info}};
    if (argc == 2 && std::string_view{argv[1]} == "inspect")
      return Options{Command::Inspect, 0, directory, utc, east};
    if (argc == 4 && std::string_view{argv[1]} == "probe" &&
        std::string_view{argv[2]} == "--sram") {
      const std::string_view width = argv[3];
      if (width == "8" || width == "16")
        return Options{Command::Probe, width == "8" ? 8u : 16u, directory, utc,
                       east};
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
  const bool ma401 = options.native_profile && nic->id == 0x800c &&
                     nic->variant == 0 && nic->major == 1 && nic->minor == 0;
  if (!ma401 && !p2::Card::supports(nic->id)) {
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
  if (options.native_profile)
    puts("PDA: CRC/NIC/SRAM validated from the fresh, owned NV backup.");
  else {
    std::array<uint8_t, P2_PDA_SIZE> pda;
    auto address = card.read_pda(pda);
    if (!address)
      return std::unexpected{address.error()};
    p2_printf("PDA: validated 1024-byte read at 0x%08lx\n",
              static_cast<unsigned long>(*address));
  }
  if (options.command != Options::Command::Probe)
    return {};
  p2_printf("Testing Genesis with %u-bit SRAM; no flash commands.\n",
            options.bits);
  const auto report = card.probe(options.bits);
  p2_printf(
      "COR before=0x%02x CCSR before=0x%02x readback=%02x %02x %02x %02x\n",
      report.cor_before, report.ccsr_before, report.readback[0],
      report.readback[1], report.readback[2], report.readback[3]);
  p2_printf("Verification reads=%lu disagreements=%lu; AUX originally %s\n",
            static_cast<unsigned long>(report.verification_reads),
            static_cast<unsigned long>(report.disagreements),
            report.aux_was_enabled ? "enabled" : "disabled");
  if (report.aux_cleanup_error)
    p2_fprintf(stderr, "Restoring AUX state: %s\n",
               p2_error_string(report.aux_cleanup_error));
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
[[maybe_unused]] p2::CardStatus inspect_work(p2::Card &card, void *context) {
  return inspect(card, *static_cast<const Options *>(context));
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
#ifdef PRISM2_AMIGA
  if (argc == 8 && std::string_view{argv[1]} == "preflight" &&
      std::string_view{argv[2]} == "--output" &&
      std::string_view{argv[4]} == "--utc" &&
      std::string_view{argv[6]} == "--utc-offset")
    return p2::preflight::run_command(argv[3], argv[5], argv[7]);
#endif
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
#if defined(PRISM2_AMIGA) || defined(PRISM2_NATIVE_TRANSACTION_TEST)
  if (options->command != Options::Command::Info && options->directory) {
    auto work_options = *options;
    work_options.native_profile = true;
    return p2::operation::run_native(
        options->directory, options->utc,
        options->command == Options::Command::Inspect ? "inspect"
        : options->bits == 16                         ? "probe-x16"
                                                      : "probe-x8",
        inspect_work, &work_options, options->offset_east);
  }
#endif
  // Refuse before opening the slot. An existing file or force option must
  // never bypass the unverified physical-flash profile/backup approval gate.
  if (options->command != Options::Command::Info) {
    p2_fprintf(stderr, "Operation blocked: a verified timestamped full-flash "
                       "backup is required.\n"
                       "Physical profile and native transaction are not "
                       "approved; no card access was attempted.\n");
    return 20;
  }
#endif
#if !defined(PRISM2_AMIGA) && !defined(PRISM2_REQUIRE_BACKUP)
  if (options->directory) {
    auto work_options = *options;
    return p2::operation::run_model(
        options->directory, options->utc,
        options->command == Options::Command::Inspect ? "inspect"
        : options->bits == 8                          ? "probe-x8"
                                                      : "probe-x16",
        inspect_work, &work_options);
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
