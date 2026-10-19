/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "bootstrap.h"
#include "card_profile.h"
#include "file.h"
#include "firmware_catalog.h"
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
  const char *firmware_directory = nullptr;
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
    const char **slot = key == "--firmware-dir" ? &options.firmware_directory
                        : key == "--initial"    ? &options.initial
                        : key == "--primary"    ? &options.primary
                        : key == "--station"    ? &options.station
                        : key == "--output"     ? &options.directory
                        : key == "--backup-dir" ? &options.backup_parent
                        : key == "--utc"        ? &options.utc
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
  if ((bool(options.directory) == bool(options.backup_parent)) ||
      (options.firmware_directory
           ? bool(options.primary || options.station || options.initial)
           : (!options.station || (options.initial && !options.primary))))
    return {};
  if (options.east == 10000)
    options.east = preflight::LocalTime;
  if (options.utc) {
    const std::string_view label{options.utc};
    if (label.empty() || label.size() > 31 ||
        !std::all_of(label.begin(), label.end(), [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        }))
      return {};
  }
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
  std::optional<firmware::Image> primary, station;
  std::optional<firmware::InitialImage> initial;
  std::optional<flash::Plan> plan;
  const char *primary_name = nullptr, *station_name = nullptr;
  const char *expected_plan = nullptr;
  profile::Observation hardware{};
  Identity running_primary{};
  BoundedVector<std::array<char, 384>, 128> firmware_paths;
  std::array<std::string_view, 128> firmware_views{};
  bool recovery_required = false;
  bool flash_changed = false;
};
std::string_view basename(std::string_view path) {
  const auto slash = path.find_last_of("/\\:");
  if (slash != path.npos)
    path.remove_prefix(slash + 1);
  return path;
}
CardStatus catalog(Context &context, const char *directory) {
  const auto lock = Lock((CONST_STRPTR)directory, ACCESS_READ);
  if (!lock) {
    p2_fprintf(stderr, "Cannot open firmware drawer: %s\n", directory);
    return std::unexpected{P2_E_IO};
  }
  auto info = make_owned<FileInfoBlock>();
  std::array<char, 384> resolved{};
  bool ok = info &&
            NameFromLock(lock, (STRPTR)resolved.data(), resolved.size()) &&
            Examine(lock, info.get()) && info->fib_DirEntryType > 0;
  while (ok && ExNext(lock, info.get())) {
    if (info->fib_DirEntryType >= 0)
      continue;
    std::string_view name{info->fib_FileName};
    if (name.size() < 4 ||
        !((name.ends_with(".hex")) || name.ends_with(".HEX")))
      continue;
    std::array<char, 384> path{};
    const auto n = p2_snprintf(path.data(), path.size(), "%s/%s",
                               resolved.data(), info->fib_FileName);
    ok = n > 0 && size_t(n) < path.size() &&
         context.firmware_paths.push_back(path);
  }
  if (ok)
    ok = IoErr() == ERROR_NO_MORE_ENTRIES;
  UnLock(lock);
  if (!ok || context.firmware_paths.empty())
    return std::unexpected{P2_E_IO};
  for (size_t i = 0; i < context.firmware_paths.size(); ++i)
    context.firmware_views[i] = context.firmware_paths[i].data();
  return {};
}
CardStatus prepare(Context &context, Card *card = nullptr) {
  auto saved = File::open("before-read-1.bin", "rb");
  auto before = ByteBuffer::allocate(flash::Bytes);
  if (!saved || !before ||
      fread(before.bytes().data(), 1, before.size(), saved.get()) !=
          before.size() ||
      !saved.close())
    return std::unexpected{P2_E_IO};
  const auto pda = before.bytes().subspan(0x10000, P2_PDA_SIZE);
  const auto hardware = profile::observe(
      {}, std::span<const uint8_t, P2_PDA_SIZE>{pda.data(), P2_PDA_SIZE});
  if (!hardware)
    return std::unexpected{hardware.error()};
  context.hardware = *hardware;
  auto snapshot = firmware::CardSnapshot::from_pda(pda);
  if (!snapshot)
    return std::unexpected{P2_E_PDA};
  if (card) {
    if (auto r = card->initialize(); !r)
      return r;
    auto nic = card->identity(P2_RID_NICID);
    auto pri = card->identity(P2_RID_PRIID);
    if (!nic || !pri || *nic != snapshot->nic)
      return std::unexpected{P2_E_VERIFY};
    context.running_primary = *pri;
    std::array<uint16_t, 5> words{};
    if (auto r = card->record(0xfd03, words); !r)
      return r;
    if (words[0] || words[1] != 3 || words[3] > words[4])
      return std::unexpected{P2_E_VERIFY};
    snapshot->suppliers[2] = firmware::InterfaceRange{
        words[0], words[1], words[2], words[3], words[4]};
  }
  if (!context.firmware_paths.empty()) {
    auto chosen = firmware::select(
        std::span{context.firmware_views}.first(context.firmware_paths.size()),
        *snapshot, pda);
    if (!chosen) {
      p2_fprintf(stderr, "%s\n", chosen.error().message);
      return std::unexpected{P2_E_VERIFY};
    }
    context.primary_name =
        chosen->primary ? context.firmware_views[chosen->primary_index].data()
                        : nullptr;
    context.station_name = context.firmware_views[chosen->station_index].data();
    context.primary = std::move(chosen->primary);
    context.station = std::move(chosen->station);
    p2_printf("Selected: %s + %s\n",
              context.primary_name ? basename(context.primary_name).data()
                                   : "current primary",
              basename(context.station_name).data());
  }
  const auto primary =
      context.primary ? std::optional{firmware::ImageSource{
                            *context.primary, basename(context.primary_name)}}
                      : std::nullopt;
  auto prepared = firmware::PreparedImage::create(
      {*context.station, basename(context.station_name)}, firmware::Mode::Flash,
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
          P2_SOURCE_REVISION,
          context.primary ? context.primary->source_sha256().data()
                          : "unchanged",
          context.station->source_sha256().data(), hash.hex().data()) < 0)
    return std::unexpected{P2_E_IO};
  if (context.initial &&
      p2_fprintf(manifest.get(), "initial=%s\n",
                 context.initial->source_sha256().data()) < 0)
    return std::unexpected{P2_E_IO};
  if (p2_fprintf(manifest.get(), "nic=%04x/%u/%u/%u\nsram_bits=%u\n",
                 context.hardware.nic.id, context.hardware.nic.variant,
                 context.hardware.nic.major, context.hardware.nic.minor,
                 context.hardware.sram_width_inference) < 0)
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
  if (auto r = prepare(context, context.initial ? nullptr : &card); !r)
    return r;
  if (context.initial) {
    const auto loaded =
        bootstrap::load(card, card.transport(), *context.initial,
                        context.hardware.sram_width_inference);
    if (loaded.error || !loaded.byte_writes_verified ||
        !loaded.initial_started) {
      context.recovery_required = loaded.recovery_required;
      return std::unexpected{loaded.error ? loaded.error : P2_E_VERIFY};
    }
  }
  const auto result =
      context.initial ? flash::program_parallel(card, card.transport(),
                                                context.plan->before(),
                                                context.plan->desired())
                      : flash::program_firmware(card, card.transport(),
                                                context.plan->before(),
                                                context.plan->desired(),
                                                context.plan->base_address());
  context.recovery_required = result.recovery_required;
  context.flash_changed =
      result.erase_command_attempted || result.program_command_attempted;
  p2_printf(
      "Firmware update: error=%d programmed=%lu verified=%lu recovery=%u\n",
      result.error, static_cast<unsigned long>(result.programmed_bytes),
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
  if (auto r = prepare(context, &card); !r)
    return r;
  if (!std::equal(context.plan->before().begin(), context.plan->before().end(),
                  context.plan->desired().begin()))
    return std::unexpected{P2_E_VERIFY};
  for (const auto &[rid, expected] :
       std::array{std::pair{P2_RID_NICID, context.hardware.nic},
                  std::pair{P2_RID_PRIID, context.primary
                                              ? context.primary->component()
                                              : context.running_primary},
                  std::pair{P2_RID_STAID, context.station->component()}}) {
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
         "  Prism2Flash program --firmware-dir Firmware --backup-dir "
         "Work:P2Backups\n"
         "  Prism2Flash verify --firmware-dir Firmware --backup-dir "
         "Work:P2Backups\n"
         "  Prism2Flash program --primary pk010101.hex --station sf010704.hex\n"
         "    --backup-dir EXISTING_DRAWER\n"
         "  Prism2Flash program --station s1010701.hex --backup-dir "
         "EXISTING_DRAWER\n"
         "  Prism2Flash program --initial ID010001.HEX --primary pk010101.hex "
         "--station sf010704.hex\n"
         "    --backup-dir EXISTING_DRAWER\n"
         "  Prism2Flash verify --primary pk010101.hex --station sf010704.hex\n"
         "    --backup-dir EXISTING_DRAWER\n"
         "Fresh full before/after captures are mandatory; no skip/force "
         "option.");
    puts("Use Stack 16384. Supply your compatible PRI/STA files. One full "
         "before and one full after capture.\n"
         "Backups are automatic. No clock setup is required.\n"
         "Optional: --expected-plan-sha SHA256; --output NEW_DRAWER instead "
         "of --backup-dir.\n");
    return argc == 2 && (std::string_view{argv[1]} == "--help" ||
                         std::string_view{argv[1]} == "help")
               ? 0
               : 20;
  }
  auto context = make_owned<Context>();
  if (!context)
    return 20;
  if (options->firmware_directory) {
    if (!catalog(*context, options->firmware_directory))
      return 20;
  } else {
    if (options->primary)
      context->primary = load<firmware::Image>(options->primary, {});
    context->station = load<firmware::Image>(options->station, {});
    if ((options->primary && !context->primary) || !context->station)
      return 20;
  }
  std::array<char, 17> generated_utc{};
  std::array<char, 384> generated_directory{};
  if (!options->utc) {
    struct DateStamp stamp {};
    DateStamp(&stamp);
    generated_utc = preflight::clock_label(
        {stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick}, options->east);
    options->utc = generated_utc.data();
  }
  if (options->backup_parent) {
    // Clock labels are cosmetic. Pick a fresh name even if time is frozen,
    // moves backwards or is unavailable; never overwrite an earlier backup.
    for (unsigned serial = 0;; ++serial) {
      const auto count =
          serial ? p2_snprintf(generated_directory.data(),
                               generated_directory.size(), "%s/P2-%s-%u",
                               options->backup_parent, options->utc, serial)
                 : p2_snprintf(generated_directory.data(),
                               generated_directory.size(), "%s/P2-%s",
                               options->backup_parent, options->utc);
      if (count <= 0 || size_t(count) >= generated_directory.size() ||
          serial >= 10000)
        return 20;
      const auto existing =
          Lock((CONST_STRPTR)generated_directory.data(), ACCESS_READ);
      if (existing) {
        UnLock(existing);
        continue;
      }
      if (IoErr() != ERROR_OBJECT_NOT_FOUND)
        return 20;
      options->directory = generated_directory.data();
      break;
    }
  }
  p2_printf("Backups: %s\n", options->directory);
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
  if (options->initial) {
    context->initial = load<firmware::InitialImage>(options->initial, {});
    if (!context->initial)
      return 20;
  }
  const auto result = operation::run_native(
      options->directory, options->utc,
      options->initial ? "flash-parallel" : "flash-firmware", program,
      context.get(), options->east, {compare, context.get()},
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
