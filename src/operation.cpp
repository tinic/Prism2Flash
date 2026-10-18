/* SPDX-License-Identifier: GPL-2.0-only */
#include "operation.h"
#include "backup.h"
#include "file.h"
#include "owned_memory.h"
#include "p2_stdio.h"
#include "sha256.h"
#include "storage.h"
#include "version.h"
#ifdef PRISM2_AMIGA
#include "card_profile.h"
#include "preflight.h"
#endif
#include <cstring>

namespace p2::operation {
namespace {
constexpr std::array Names{"before-read-1.bin", "before-read-2.bin",
                           "after-read-1.bin",  "after-read-2.bin",
                           "before.json",       "after.json",
                           "transaction.json"};
// The model uses a synthetic profile. Native admission is confined to the
// user-accepted MA401 8 MiB NV map and a RAM-only x16 operation.
class CaptureJournal final : public Journal {
public:
  CaptureJournal(const char *directory, const char *utc, const char *operation,
                 bool native = false, int offset_east = 10000,
                 Expected expected = {}) noexcept
      : directory_(directory), utc_(utc), operation_(operation),
        native_(native), offset_east_(offset_east), expected_(expected) {}
  CardStatus prepare() override {
    // Only the qualified MA401 operations reach native storage/slot
    // acquisition.
    if (native_) {
#ifdef PRISM2_AMIGA
      // Admission is narrow; fresh same-claim captures, exact PDA/CIS/input
      // checks and runtime bootstrap/JEDEC proof remain mandatory for flash.
      const bool flash = !std::strcmp(operation_, "flash-sst");
      if ((std::strcmp(operation_, "probe-x16") &&
           std::strcmp(operation_, "bootstrap-x16") &&
           std::strcmp(operation_, "verify-current") && !flash) ||
          offset_east_ < -840 || offset_east_ > 840)
        return std::unexpected{P2_E_UNSUPPORTED};
      if (flash && (!expected_.compare || !profile::programming_approved()))
        return std::unexpected{P2_E_UNSUPPORTED};
      auto session =
          preflight::NativeSession::create(directory_, utc_, offset_east_);
      if (!session)
        return std::unexpected{session.error()};
      session_ = std::move(*session);
      created_ = true;
#else
      return std::unexpected{P2_E_UNSUPPORTED};
#endif
    }
    if (!backup::valid_utc(utc_) || !std::strstr(directory_, utc_) ||
        std::strlen(directory_) > 320)
      return std::unexpected{P2_E_ARGUMENT};
    if (!native_ && p2_platform_backup_directory(directory_))
      return std::unexpected{P2_E_IO};
    created_ = true;
    // Preflight all outputs before claiming the card. A later write/close can
    // still fail; such errors remain failures and preserve partial evidence.
    for (std::size_t step = 0; step < paths_.size(); ++step) {
      // Reserve the failure record first, so later output-open failures can
      // still be recorded without claiming a slot.
      const std::size_t i = step ? step - 1 : 6;
      if (native_)
        p2_snprintf(paths_[i].data(), paths_[i].size(), "%s", Names[i]);
      else
        p2_snprintf(paths_[i].data(), paths_[i].size(), "%s/%s", directory_,
                    Names[i]);
      if (native_ && (i == 1 || i == 3))
        continue;
      files_[i] = File::open(paths_[i].data(), "w+b");
      if (!files_[i]) {
        for (std::size_t raw = 0; raw < 6; ++raw)
          (void)files_[raw].close();
        return std::unexpected{P2_E_IO};
      }
    }
    return {};
  }
  CardStatus capture(p2_bus &bus, Phase phase,
                     const p2_card_info &info) override {
    if (native_ ? (info.manufacturer != 0xb || info.product != 0x7300)
                : (info.manufacturer != 0x0274 || info.product != 0x1613))
      return std::unexpected{P2_E_UNSUPPORTED};
    const std::size_t index = phase == Phase::Before ? 0 : 1;
    // A journal is single-use, with a strict before/after sequence.
    if (attempted_[index] || (index && !verified_[0]))
      return std::unexpected{P2_E_ARGUMENT};
    attempted_[index] = true;
    auto &result = captures_[index];
    if (native_)
      p2_printf("Saving full %s capture...\n", index ? "after" : "before");
    const auto started = bus.now_us(bus.ctx);
    std::array<char, 17> started_utc{}, ended_utc{};
    int timestamp_error = 0;
#ifdef PRISM2_AMIGA
    if (native_) {
      auto stamp = session_->stamp(index ? preflight::Checkpoint::AfterStart
                                         : preflight::Checkpoint::BeforeStart,
                                   bus);
      if (stamp)
        started_utc = *stamp;
      else
        timestamp_error = stamp.error();
    }
#endif
    if (!timestamp_error)
      result = native_
                   ? backup::capture_single(bus, *files_[index * 2], 0x20000)
                   : backup::capture(bus, *files_[index * 2],
                                     *files_[index * 2 + 1]);
    const auto ended = bus.now_us(bus.ctx);
    int error = timestamp_error ? timestamp_error : result.error;
    for (std::size_t i = index * 2; i < index * 2 + 2; ++i)
      if (!files_[i].close() && !error)
        error = P2_E_IO;
    bool saved = false;
    if (!error) {
      error = backup::verify_file(paths_[index * 2].data(),
                                  result.sha256[0].data());
      if (!error && !native_)
        error = backup::verify_file(paths_[index * 2 + 1].data(),
                                    result.sha256[1].data());
      saved = !error;
    }
#ifdef PRISM2_AMIGA
    if (native_) {
      auto stamp = session_->stamp(index ? preflight::Checkpoint::AfterEnd
                                         : preflight::Checkpoint::BeforeEnd,
                                   bus);
      if (stamp)
        ended_utc = *stamp;
      else {
        timestamp_error = stamp.error();
        if (!error)
          error = timestamp_error;
      }
      if (!index && !error)
        error = validate_native(info);
    }
#endif
    auto storage = make_owned<std::array<char, 3072>>();
    if (!storage)
      return std::unexpected{P2_E_IO};
    auto &text = *storage;
    std::array<char, 64> byte_counts{};
    std::array<char, 144> hashes{};
    if (native_) {
      p2_snprintf(byte_counts.data(), byte_counts.size(), "[%lu]",
                  static_cast<unsigned long>(result.bytes[0]));
      p2_snprintf(hashes.data(), hashes.size(), "[\"%s\"]",
                  result.sha256[0].data());
    } else {
      p2_snprintf(byte_counts.data(), byte_counts.size(), "[%lu, %lu]",
                  static_cast<unsigned long>(result.bytes[0]),
                  static_cast<unsigned long>(result.bytes[1]));
      p2_snprintf(hashes.data(), hashes.size(), "[\"%s\", \"%s\"]",
                  result.sha256[0].data(), result.sha256[1].data());
    }
    const auto count = p2_snprintf(
        text.data(), text.size(),
        "{\n  \"format\": \"Prism2 transaction capture 2\",\n"
        "  \"tool_version\": \"" P2_VERSION "\",\n"
        "  \"source_revision\": \"" P2_SOURCE_REVISION "\",\n"
        "  \"simulation_only\": %s,\n  \"hardware_authorization\": false,\n"
        "  \"profile\": \"%s\",\n"
        "  \"operation\": \"%s\",\n  \"phase\": \"%s\",\n"
        "  \"utc_supplied\": \"%s\",\n"
        "  \"started_utc\": \"%s\",\n  \"ended_utc\": \"%s\",\n"
        "  \"utc_offset_minutes_east\": %d,\n"
        "  \"timestamp_error\": %d,\n"
        "  \"timestamp_basis\": \"%s\",\n"
        "  \"started_us\": %lu,\n  \"ended_us\": %lu,\n"
        "  \"cis_manufacturer\": %u,\n  \"cis_product\": %u,\n"
        "  \"selector\": 1,\n  \"range\": [0, 8388608],\n"
        "  \"byte_order\": \"little-endian word bytes\",\n"
        "  \"capture_count\": %u,\n"
        "  \"alias_bytes\": %lu,\n"
        "  \"bytes_read\": %s,\n"
        "  \"sha256\": %s,\n"
        "  \"capture_error\": %d,\n  \"cleanup_error\": %d,\n"
        "  \"saved_file_error\": %d,\n"
        "  \"files_reopened_and_verified\": %s\n}\n",
        native_ ? "false" : "true",
        native_ ? "ma401-aux-nv-8m-ram-probe-v1"
                : "protocol-model-nv-linear-8m-v1",
        operation_, index ? "after" : "before", utc_, started_utc.data(),
        ended_utc.data(), offset_east_, timestamp_error,
        native_ ? "AmigaDOS DateStamp, explicit east offset; UTC checked at "
                  "start and wall clock compared with monotonic timer at "
                  "every capture boundary"
                : "operator-supplied UTC, unverified; start/end are wrapping "
                  "model microseconds, not UTC",
        static_cast<unsigned long>(started), static_cast<unsigned long>(ended),
        info.manufacturer, info.product, native_ ? 1u : 2u,
        native_ ? 0x20000ul : 0ul, byte_counts.data(), hashes.data(),
        result.error, result.cleanup_error, error, saved ? "true" : "false");
    const int manifest_error = save_text(4 + index, text, count);
    if (!error)
      error = manifest_error;
    verified_[index] = !error;
    return error ? CardStatus{std::unexpected{static_cast<p2_error>(error)}}
                 : CardStatus{};
  }
  CardStatus compare() override {
    if (!verified_[0] || !verified_[1])
      return std::unexpected{P2_E_VERIFY};
    // Recheck on-disk content, including the original backup, before comparing.
    for (std::size_t i = 0; i < 4; ++i) {
      if (native_ && (i == 1 || i == 3))
        continue;
      if (int error = backup::verify_file(
              paths_[i].data(), captures_[i / 2].sha256[i % 2].data()))
        return std::unexpected{static_cast<p2_error>(error)};
    }
    if (expected_.compare)
      return expected_.compare(paths_[0].data(), paths_[2].data(),
                               expected_.context);
    auto before = File::open(paths_[0].data(), "rb");
    auto after = File::open(paths_[2].data(), "rb");
    auto buffer = ByteBuffer::allocate(8192);
    if (!before || !after || !buffer)
      return std::unexpected{P2_E_IO};
    bool equal = true;
    for (uint32_t address = 0; address < backup::NvBytes; address += 4096) {
      if (fread(buffer.bytes().data(), 1, 4096, before.get()) != 4096 ||
          fread(buffer.bytes().data() + 4096, 1, 4096, after.get()) != 4096)
        return std::unexpected{P2_E_IO};
      equal &= !std::memcmp(buffer.bytes().data(), buffer.bytes().data() + 4096,
                            4096);
    }
    const bool closed_before = before.close(), closed_after = after.close();
    if (!closed_before || !closed_after)
      return std::unexpected{P2_E_IO};
    return equal ? CardStatus{} : CardStatus{std::unexpected{P2_E_VERIFY}};
  }
  CardStatus finish(const Report &result) override {
    if (!created_)
      return {};
    if (!files_[6])
      return std::unexpected{P2_E_IO};
    auto storage = make_owned<std::array<char, 2048>>();
    if (!storage)
      return std::unexpected{P2_E_IO};
    auto &text = *storage;
    const auto count = p2_snprintf(
        text.data(), text.size(),
        "{\n  \"format\": \"Prism2 transaction 2\",\n"
        "  \"source_revision\": \"" P2_SOURCE_REVISION "\",\n"
        "  \"simulation_only\": %s,\n  \"hardware_authorization\": false,\n"
        "  \"operation\": \"%s\",\n  \"utc_supplied\": \"%s\",\n"
        "  \"preparation_error\": %d,\n  \"acquisition_error\": %d,\n"
        "  \"operation_started\": %s,\n  \"operation_error\": %d,\n"
        "  \"readback_attempted\": %s,\n  \"readback_error\": %d,\n"
        "  \"comparison_error\": %d,\n  \"cleanup_error\": %d,\n"
        "  \"recovery_session_held\": %s,\n"
        "  \"cleanup_pending\": %s,\n"
        "  \"success\": %s\n}\n",
        native_ ? "false" : "true", operation_, utc_, result.preparation_error,
        result.acquisition_error, result.operation_started ? "true" : "false",
        result.operation_error, result.readback_attempted ? "true" : "false",
        result.readback_error, result.comparison_error, result.cleanup_error,
        result.recovery_held ? "true" : "false",
        result.recovery_held ? "true" : "false",
        result.success() ? "true" : "false");
    const int error = save_text(6, text, count);
    return error ? CardStatus{std::unexpected{static_cast<p2_error>(error)}}
                 : CardStatus{};
  }

  CardStatus record_release(const Report &result) {
    // Preserve the verified held-state journal. Record later cleanup in a
    // separate checked file after explicit release, without truncating it.
    constexpr auto name = "recovery-release.json";
    auto storage = make_owned<std::array<char, 1024>>();
    auto file = File::open(name, "w+b");
    if (!storage || !file)
      return std::unexpected{P2_E_IO};
    const auto count =
        p2_snprintf(storage->data(), storage->size(),
                    "{\n  \"format\": \"Prism2 recovery release 1\",\n"
                    "  \"source_revision\": \"" P2_SOURCE_REVISION "\",\n"
                    "  \"utc_transaction_start\": \"%s\",\n"
                    "  \"cleanup_pending\": false,\n  \"cleanup_error\": %d,\n"
                    "  \"operation_error\": %d,\n  \"readback_error\": %d,\n"
                    "  \"comparison_error\": %d,\n"
                    "  \"ownership_released_without_reset\": true\n}\n",
                    utc_, result.cleanup_error, result.operation_error,
                    result.readback_error, result.comparison_error);
    if (count <= 0 || static_cast<std::size_t>(count) >= storage->size())
      return std::unexpected{P2_E_IO};
    Sha256 hash;
    hash.update(
        std::string_view{storage->data(), static_cast<std::size_t>(count)});
    if (fwrite(storage->data(), 1, count, file.get()) !=
            static_cast<std::size_t>(count) ||
        fflush(file.get()) || !file.close())
      return std::unexpected{P2_E_IO};
    const auto error = backup::verify_file(name, hash.hex().data(), count);
    return error ? CardStatus{std::unexpected{static_cast<p2_error>(error)}}
                 : CardStatus{};
  }

private:
#ifdef PRISM2_AMIGA
  int validate_native(const p2_card_info &info) {
    auto file = File::open(paths_[0].data(), "rb");
    auto pda = make_owned<std::array<uint8_t, P2_PDA_SIZE>>();
    if (!file || !pda || fseek(file.get(), 0x7f0000, SEEK_SET) ||
        fread(pda->data(), 1, pda->size(), file.get()) != pda->size())
      return P2_E_IO;
    auto observed = profile::observe(info, *pda);
    if (!observed || observed->sram_width_inference != 16)
      return observed ? P2_E_UNSUPPORTED : observed.error();
    return file.close() ? P2_OK : P2_E_IO;
  }
  // Declared before streams: they close before CurrentDir/locks are released.
  Owned<preflight::NativeSession> session_;
#endif
  int save_text(std::size_t index, std::span<const char> text, int count) {
    if (count <= 0 || static_cast<std::size_t>(count) >= text.size() ||
        !files_[index])
      return P2_E_IO;
    const auto bytes = text.first(static_cast<std::size_t>(count));
    Sha256 hash;
    hash.update(std::string_view{bytes.data(), bytes.size()});
    const bool written = fwrite(bytes.data(), 1, bytes.size(),
                                files_[index].get()) == bytes.size();
    const bool flushed = fflush(files_[index].get()) == 0;
    const bool closed = files_[index].close();
    if (!written || !flushed || !closed)
      return P2_E_IO;
    const auto expected = hash.hex();
    return backup::verify_file(paths_[index].data(), expected.data(), count);
  }
  const char *directory_, *utc_, *operation_;
  bool native_ = false, created_ = false;
  int offset_east_ = 10000;
  Expected expected_;
  std::array<std::array<char, 384>, 7> paths_{};
  std::array<File, 7> files_;
  std::array<backup::CaptureReport, 2> captures_;
  std::array<bool, 2> attempted_{}, verified_{};
};
} // namespace

#if !defined(PRISM2_AMIGA) && !defined(PRISM2_NATIVE_TRANSACTION_TEST)
Owned<Journal> make_model_journal(const char *directory, const char *utc,
                                  const char *operation) {
  if (!directory || !utc || !operation)
    return {};
  auto journal = make_owned<CaptureJournal>(directory, utc, operation);
  return Owned<Journal>{journal.release()};
}

int run_model(const char *directory, const char *utc, const char *operation,
              Work work, void *context) {
  if (!directory || !utc || !operation ||
      (std::strcmp(operation, "inspect") &&
       std::strcmp(operation, "probe-x8") &&
       std::strcmp(operation, "probe-x16")))
    return 20;
  // No journal implementation or CLI profile can enable physical hardware.
  auto journal = make_model_journal(directory, utc, operation);
  if (!journal)
    return 20;
  const auto result = run(*journal, work, context);
  p2_printf("MODEL transaction: before=%d operation=%d after=%d compare=%d "
            "cleanup=%d journal=%d preparation=%d\n",
            result.acquisition_error, result.operation_error,
            result.readback_error, result.comparison_error,
            result.cleanup_error, result.journal_error,
            result.preparation_error);
  if (result.success())
    puts(
        "Model operation passed with saved before/after NV captures unchanged. "
        "No physical-card approval is implied.");
  return result.success() ? 0 : 20;
}
#else
int run_native(const char *directory, const char *utc, const char *operation,
               Work work, void *context, int offset_east, Expected expected,
               Recovery recovery) {
  if (!directory || !utc || !operation || !work)
    return 20;
  auto journal = make_owned<CaptureJournal>(directory, utc, operation, true,
                                            offset_east, expected);
  if (!journal)
    return 20;
  auto result = run(*journal, work, context, recovery);
  if (result.recovery_held)
    if (auto record = journal->record_release(result); !record)
      result.journal_error = record.error();
  if (result.preparation_error) {
    p2_fprintf(stderr,
               "Operation blocked: a verified timestamped full-flash backup is "
               "required. Native preparation failed (%d); "
               "no card access was attempted.\n",
               result.preparation_error);
    return 20;
  }
  p2_printf("Native transaction: before=%d operation=%d after=%d compare=%d "
            "cleanup=%d journal=%d preparation=%d\n",
            result.acquisition_error, result.operation_error,
            result.readback_error, result.comparison_error,
            result.cleanup_error, result.journal_error,
            result.preparation_error);
  return result.success() ? 0 : 20;
}
#endif
} // namespace p2::operation
