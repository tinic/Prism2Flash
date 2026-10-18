/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "bootstrap.h"
#include "card_profile.h"
#include "file.h"
#include "flash_plan.h"
#include "operation.h"
#include "p2_stdio.h"
#include "preflight.h"
#include "sha256.h"
#include "version.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

namespace {
using namespace p2;
struct Options {
  bool verify = false;
  const char *initial = nullptr, *primary = nullptr, *station = nullptr;
  const char *directory = nullptr, *utc = nullptr;
  const char *backup_parent = nullptr;
  const char *expected_plan = nullptr;
  int east = 10000;
};
std::optional<Options> parse(int argc, char **argv) {
  if (argc < 2 || (std::string_view{argv[1]} != "program" &&
                   std::string_view{argv[1]} != "verify"))
    return {};
  Options options;
  options.verify = std::string_view{argv[1]} == "verify";
  for (int i = 2; i < argc; i += 2) {
    if (i + 1 == argc)
      return {};
    const std::string_view key = argv[i], value = argv[i + 1];
    const char **slot = key == "--initial"             ? &options.initial
                        : key == "--primary"           ? &options.primary
                        : key == "--station"           ? &options.station
                        : key == "--output"            ? &options.directory
                        : key == "--backup-dir"        ? &options.backup_parent
                        : key == "--utc"               ? &options.utc
                        : key == "--expected-plan-sha" ? &options.expected_plan
                                                       : nullptr;
    if (slot) {
      if (*slot)
        return {};
      *slot = argv[i + 1];
    } else if (key == "--utc-offset" && options.east == 10000) {
      const auto result = std::from_chars(
          value.data(), value.data() + value.size(), options.east);
      if (result.ec != std::errc{} ||
          result.ptr != value.data() + value.size() || options.east < -840 ||
          options.east > 840)
        return {};
    } else
      return {};
  }
  if (!options.primary || !options.station ||
      (bool(options.directory) == bool(options.backup_parent)) ||
      (options.directory && !options.utc) ||
      (options.utc && options.east == 10000) ||
      (!options.verify && !options.initial))
    return {};
  if (options.east == 10000)
    options.east = preflight::LocalTime;
  if (options.expected_plan) {
    const std::string_view digest = options.expected_plan;
    if (digest.size() != 64 ||
        !std::all_of(digest.begin(), digest.end(), [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
      return {};
  }
  return options;
}
template <typename Image>
std::optional<Image> load(const char *path, std::string_view expected) {
  auto file = File::open(path, "rb");
  if (!file) {
    p2_fprintf(stderr, "Cannot open firmware file: %s\n", path);
    return {};
  }
  auto image = Image::read(*file);
  if (!image || !file.close() ||
      (!expected.empty() && image->source_sha256() != expected)) {
    p2_fprintf(stderr, "Invalid or unsupported firmware input: %s\n", path);
    return {};
  }
  return std::move(*image);
}
struct Context {
  firmware::Image primary, station;
  std::optional<firmware::InitialImage> initial;
  std::optional<flash::Plan> plan;
  const char *primary_name = nullptr, *station_name = nullptr;
  const char *expected_plan = nullptr;
  bool recovery_required = false;
  bool flash_changed = false;
};
std::string_view basename(std::string_view path) {
  const auto slash = path.find_last_of("/\\:");
  if (slash != path.npos)
    path.remove_prefix(slash + 1);
  return path;
}
CardStatus prepare(Context &context) {
  auto saved = File::open("before-read-1.bin", "rb");
  auto before = ByteBuffer::allocate(flash::Bytes);
  if (!saved || !before ||
      fread(before.bytes().data(), 1, before.size(), saved.get()) !=
          before.size() ||
      !saved.close())
    return std::unexpected{P2_E_IO};
  const auto pda = before.bytes().subspan(0x10000, P2_PDA_SIZE);
  const auto snapshot = firmware::CardSnapshot::from_pda(pda);
  if (!snapshot)
    return std::unexpected{P2_E_PDA};
  const auto primary =
      firmware::ImageSource{context.primary, basename(context.primary_name)};
  auto prepared = firmware::PreparedImage::create(
      {context.station, basename(context.station_name)}, firmware::Mode::Flash,
      *snapshot, pda, primary, firmware::PlugPolicy::DosOptionalDefaults);
  if (!prepared) {
    p2_fprintf(stderr, "Fresh-PDA preparation: %s\n", prepared.error().message);
    return std::unexpected{P2_E_VERIFY};
  }
  auto plan = flash::Plan::create(before.bytes(), *prepared);
  if (!plan)
    return std::unexpected{plan.error()};
  context.plan.emplace(std::move(*plan));
  auto file = File::open("planned-flash.bin", "w+b");
  Sha256 hash;
  hash.update(context.plan->desired());
  if (context.expected_plan &&
      std::string_view{hash.hex().data()} != context.expected_plan) {
    p2_fprintf(stderr, "Prepared flash image differs from expected SHA256; no "
                       "bootstrap/erase.\n");
    return std::unexpected{P2_E_VERIFY};
  }
  if (!file ||
      fwrite(context.plan->desired().data(), 1, flash::Bytes, file.get()) !=
          flash::Bytes ||
      fflush(file.get()) || !file.close() ||
      backup::verify_file("planned-flash.bin", hash.hex().data(), flash::Bytes))
    return std::unexpected{P2_E_IO};
  auto manifest = File::open("flash-plan.txt", "w+b");
  if (!manifest)
    return std::unexpected{P2_E_IO};
  if (p2_fprintf(
          manifest.get(),
          "Prism2 flash plan 1\nsource=%s\nprimary=%s\nstation=%s\nimage=%s\n",
          P2_SOURCE_REVISION, context.primary.source_sha256().data(),
          context.station.source_sha256().data(), hash.hex().data()) < 0)
    return std::unexpected{P2_E_IO};
  for (const auto &fallback : prepared->fallbacks())
    if (p2_fprintf(manifest.get(),
                   "DOS-default PDR=%04lx expected=%lu actual=%lu missing=%u\n",
                   static_cast<unsigned long>(fallback.pdr),
                   static_cast<unsigned long>(fallback.expected_bytes),
                   static_cast<unsigned long>(fallback.actual_bytes),
                   fallback.missing) < 0)
      return std::unexpected{P2_E_IO};
  return fflush(manifest.get()) || !manifest.close()
             ? CardStatus{std::unexpected{P2_E_IO}}
             : CardStatus{};
}
CardStatus program(Card &card, void *opaque) {
  auto &context = *static_cast<Context *>(opaque);
  if (auto r = prepare(context); !r)
    return r;
  const auto loaded = bootstrap::load(card, card.transport(), *context.initial);
  p2_printf("Initial code: error=%d byte-lanes=%u verified=%lu started=%u\n",
            loaded.error, loaded.byte_writes_verified,
            static_cast<unsigned long>(loaded.bytes_verified),
            loaded.initial_started);
  if (loaded.error || !loaded.byte_writes_verified || !loaded.initial_started) {
    context.recovery_required = loaded.recovery_required;
    return std::unexpected{loaded.error ? loaded.error : P2_E_VERIFY};
  }
  const auto result = flash::program_sst(
      card, card.transport(), context.plan->before(), context.plan->desired());
  context.recovery_required = result.recovery_required;
  context.flash_changed = result.erase_command_attempted;
  p2_printf("Flash: error=%d manufacturer=%02x device=%02x erase_attempted=%u "
            "erase_command_completed=%u "
            "programmed=%lu verified=%lu recovery=%u\n",
            result.error, result.manufacturer, result.device,
            result.erase_command_attempted, result.erase_started,
            static_cast<unsigned long>(result.programmed_bytes),
            static_cast<unsigned long>(result.verified_bytes),
            result.recovery_required);
  return result.error ? CardStatus{std::unexpected{result.error}}
                      : CardStatus{};
}
CardStatus compare(const char *, const char *path, void *opaque) {
  const auto &context = *static_cast<Context *>(opaque);
  if (!context.plan)
    return std::unexpected{P2_E_VERIFY};
  auto file = File::open(path, "rb");
  auto buffer = ByteBuffer::allocate(4096);
  if (!file || !buffer)
    return std::unexpected{P2_E_IO};
  for (uint32_t offset = 0; offset < backup::NvBytes; offset += buffer.size()) {
    if (fread(buffer.bytes().data(), 1, buffer.size(), file.get()) !=
        buffer.size())
      return std::unexpected{P2_E_IO};
    const auto expected =
        context.plan->desired().subspan(offset % flash::Bytes, buffer.size());
    if (!std::equal(buffer.bytes().begin(), buffer.bytes().end(),
                    expected.begin()))
      return std::unexpected{P2_E_VERIFY};
  }
  return file.close() ? CardStatus{} : CardStatus{std::unexpected{P2_E_IO}};
}
CardStatus verify(Card &card, void *opaque) {
  auto &context = *static_cast<Context *>(opaque);
  if (auto r = prepare(context); !r)
    return r;
  if (!std::equal(context.plan->before().begin(), context.plan->before().end(),
                  context.plan->desired().begin()))
    return std::unexpected{P2_E_VERIFY};
  if (auto r = card.initialize(); !r)
    return r;
  for (const auto &[rid, expected] :
       std::array{std::pair{P2_RID_NICID, Identity{0x800c, 0, 1, 0}},
                  std::pair{P2_RID_PRIID, context.primary.component()},
                  std::pair{P2_RID_STAID, context.station.component()}}) {
    auto observed = card.identity(rid);
    if (!observed)
      return std::unexpected{observed.error()};
    p2_printf("RID=%04x id=%04x variant=%u major=%u minor=%u match=%u\n", rid,
              observed->id, observed->variant, observed->major, observed->minor,
              *observed == expected);
    if (*observed != expected)
      return std::unexpected{P2_E_VERIFY};
  }
  return {};
}
void hold(Card &card, void *) {
  p2_fprintf(stderr, "Recovery required: card ownership and RAM retained. No "
                     "automatic reset/retry.\n"
                     "Keep power on. Ctrl-E explicitly releases ownership "
                     "without resetting.\n");
  fflush(stderr);
  while (card.transport().present(card.transport().ctx))
    if (Wait(SIGBREAKF_CTRL_E) & SIGBREAKF_CTRL_E)
      break;
}
} // namespace
const char p2_flash_version_tag[] __attribute__((used)) =
    "\0$VER: Prism2Flash " P2_AMIGA_VERSION " (" P2_AMIGA_DATE ") " P2_VERSION;
int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    puts("Prism2Flash " P2_VERSION);
    return 0;
  }
  auto options = parse(argc, argv);
  if (!options) {
    puts("Prism2Flash " P2_VERSION "\n"
         "  Prism2Flash program --initial ID010001.HEX --primary pk010101.hex "
         "--station sf010704.hex\n"
         "    --backup-dir EXISTING_DRAWER\n"
         "  Prism2Flash verify --primary pk010101.hex --station sf010704.hex\n"
         "    --backup-dir EXISTING_DRAWER\n"
         "Fresh full before/after captures are mandatory; no skip/force "
         "option.");
    puts(
        "Use Stack 16384. Supply your compatible PRI/STA files. One full "
        "before and one full after capture.\n"
        "Backup drawers use the Amiga local date/time automatically (L "
        "suffix).\n"
        "Optional: --expected-plan-sha SHA256; advanced: --output NEW_DRAWER "
        "--utc YYYYMMDDTHHMMSSZ --utc-offset MINUTES_EAST instead of "
        "--backup-dir. --utc-offset alone generates automatic UTC timestamps.");
    return argc == 2 && (std::string_view{argv[1]} == "--help" ||
                         std::string_view{argv[1]} == "help")
               ? 0
               : 20;
  }
  std::array<char, 17> generated_utc{};
  std::array<char, 384> generated_directory{};
  if (!options->utc) {
    struct DateStamp stamp {};
    DateStamp(&stamp);
    const auto utc = preflight::format_clock(
        {stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick}, options->east);
    if (!utc) {
      p2_fprintf(stderr, "Cannot read a valid Amiga date/time (2020-2099).\n");
      return 20;
    }
    generated_utc = *utc;
    options->utc = generated_utc.data();
  }
  if (options->backup_parent) {
    const auto count =
        p2_snprintf(generated_directory.data(), generated_directory.size(),
                    "%s/P2-%s", options->backup_parent, options->utc);
    if (count <= 0 ||
        static_cast<std::size_t>(count) >= generated_directory.size())
      return 20;
    options->directory = generated_directory.data();
  }
  p2_printf("Backups: %s\n", options->directory);
  if (!options->verify && !profile::programming_approved()) {
    p2_fprintf(
        stderr,
        "Native programming admission remains disabled; no card access.\n");
    return 20;
  }
  auto primary = load<firmware::Image>(options->primary, {});
  auto station = load<firmware::Image>(options->station, {});
  if (!primary || !station)
    return 20;
  const auto pri = primary->component(), sta = station->component();
  p2_printf("Target PRI %u.%u.%u / STA %u.%u.%u\n", pri.major, pri.minor,
            pri.variant, sta.major, sta.minor, sta.variant);
  auto context = make_owned<Context>(std::move(*primary), std::move(*station));
  if (!context)
    return 20;
  context->primary_name = options->primary;
  context->station_name = options->station;
  context->expected_plan = options->expected_plan;
  if (options->verify) {
    const auto result = operation::run_native(
        options->directory, options->utc, "verify-current", verify,
        context.get(), options->east, {compare, context.get()});
    if (!result)
      puts("SUCCESS: running firmware and full flash contents verified.");
    return result;
  }
  context->initial = load<firmware::InitialImage>(
      options->initial,
      "1d326cee5887005e9b755e74e5578ccfbd74ae5e7ebc19c1af23e61b7a76c018");
  if (!context->initial)
    return 20;
  const auto result = operation::run_native(
      options->directory, options->utc, "flash-sst", program, context.get(),
      options->east, {compare, context.get()},
      {[](void *c, const operation::Report &r) {
         const auto &ctx = *static_cast<Context *>(c);
         return ctx.recovery_required ||
                (ctx.flash_changed &&
                 (!r.readback_attempted || r.operation_error ||
                  r.readback_error || r.comparison_error));
       },
       hold});
  if (!result)
    puts("SUCCESS: target firmware and full flash readback verified.");
  return result;
}
