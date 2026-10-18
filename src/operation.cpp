/* SPDX-License-Identifier: GPL-2.0-only */
#include "operation.h"
#include "backup.h"
#include "file.h"
#include "owned_memory.h"
#include "p2_stdio.h"
#include "sha256.h"
#include "storage.h"
#include "version.h"
#include <cstring>

namespace p2::operation {
namespace {
// Explicitly a synthetic profile. The actual MA401 profile has no native
// approval and cannot be selected through this journal or any CLI option.
class CaptureJournal final : public Journal {
public:
  CaptureJournal(const char *directory, const char *utc, const char *operation,
                 bool native = false) noexcept
      : directory_(directory), utc_(utc), operation_(operation),
        native_(native) {}
  CardStatus prepare() override {
    // A candidate observation cannot grant a physical operation. Refuse before
    // creating outputs or opening the slot until independent approval exists.
    if (native_)
      return std::unexpected{P2_E_UNSUPPORTED};
    if (!backup::valid_utc(utc_) || !std::strstr(directory_, utc_) ||
        std::strlen(directory_) > 320)
      return std::unexpected{P2_E_ARGUMENT};
    if (p2_platform_backup_directory(directory_))
      return std::unexpected{P2_E_IO};
    created_ = true;
    constexpr std::array names{"before-read-1.bin", "before-read-2.bin",
                               "after-read-1.bin",  "after-read-2.bin",
                               "before.json",       "after.json",
                               "transaction.json"};
    // Preflight all outputs before claiming the card. A later write/close can
    // still fail; such errors remain failures and preserve partial evidence.
    for (std::size_t step = 0; step < paths_.size(); ++step) {
      // Reserve the failure record first, so later output-open failures can
      // still be recorded without claiming a slot.
      const std::size_t i = step ? step - 1 : 6;
      p2_snprintf(paths_[i].data(), paths_[i].size(), "%s/%s", directory_,
                  names[i]);
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
    if (info.manufacturer != 0x0274 || info.product != 0x1613)
      return std::unexpected{P2_E_UNSUPPORTED};
    const std::size_t index = phase == Phase::Before ? 0 : 1;
    // A journal is single-use, with a strict before/after sequence.
    if (attempted_[index] || (index && !verified_[0]))
      return std::unexpected{P2_E_ARGUMENT};
    attempted_[index] = true;
    auto &result = captures_[index];
    const auto started = bus.now_us(bus.ctx);
    result = backup::capture(bus, *files_[index * 2], *files_[index * 2 + 1]);
    const auto ended = bus.now_us(bus.ctx);
    int error = result.error;
    for (std::size_t i = index * 2; i < index * 2 + 2; ++i)
      if (!files_[i].close() && !error)
        error = P2_E_IO;
    bool saved = false;
    if (!error) {
      error = backup::verify_file(paths_[index * 2].data(),
                                  result.sha256[0].data());
      if (!error)
        error = backup::verify_file(paths_[index * 2 + 1].data(),
                                    result.sha256[1].data());
      saved = !error;
    }
    std::array<char, 2048> text;
    const auto count = p2_snprintf(
        text.data(), text.size(),
        "{\n  \"format\": \"Prism2 model transaction capture 1\",\n"
        "  \"tool_version\": \"" P2_VERSION "\",\n"
        "  \"source_revision\": \"" P2_SOURCE_REVISION "\",\n"
        "  \"simulation_only\": true,\n  \"hardware_authorization\": false,\n"
        "  \"profile\": \"protocol-model-nv-linear-8m-v1\",\n"
        "  \"operation\": \"%s\",\n  \"phase\": \"%s\",\n"
        "  \"utc_supplied\": \"%s\",\n"
        "  \"timestamp_basis\": \"operator-supplied UTC, unverified; "
        "start/end are wrapping model microseconds, not UTC\",\n"
        "  \"started_us\": %lu,\n  \"ended_us\": %lu,\n"
        "  \"cis_manufacturer\": %u,\n  \"cis_product\": %u,\n"
        "  \"selector\": 1,\n  \"range\": [0, 8388608],\n"
        "  \"byte_order\": \"little-endian word bytes\",\n"
        "  \"bytes_read\": [%lu, %lu],\n"
        "  \"sha256\": [\"%s\", \"%s\"],\n"
        "  \"capture_error\": %d,\n  \"cleanup_error\": %d,\n"
        "  \"saved_file_error\": %d,\n"
        "  \"files_reopened_and_verified\": %s\n}\n",
        operation_, index ? "after" : "before", utc_,
        static_cast<unsigned long>(started), static_cast<unsigned long>(ended),
        info.manufacturer, info.product,
        static_cast<unsigned long>(result.bytes[0]),
        static_cast<unsigned long>(result.bytes[1]), result.sha256[0].data(),
        result.sha256[1].data(), result.error, result.cleanup_error, error,
        saved ? "true" : "false");
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
    for (std::size_t i = 0; i < 4; ++i)
      if (int error = backup::verify_file(
              paths_[i].data(), captures_[i / 2].sha256[i % 2].data()))
        return std::unexpected{static_cast<p2_error>(error)};
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
    std::array<char, 2048> text;
    const auto count = p2_snprintf(
        text.data(), text.size(),
        "{\n  \"format\": \"Prism2 model transaction 1\",\n"
        "  \"source_revision\": \"" P2_SOURCE_REVISION "\",\n"
        "  \"simulation_only\": true,\n  \"hardware_authorization\": false,\n"
        "  \"operation\": \"%s\",\n  \"utc_supplied\": \"%s\",\n"
        "  \"preparation_error\": %d,\n  \"acquisition_error\": %d,\n"
        "  \"operation_started\": %s,\n  \"operation_error\": %d,\n"
        "  \"readback_attempted\": %s,\n  \"readback_error\": %d,\n"
        "  \"comparison_error\": %d,\n  \"cleanup_error\": %d,\n"
        "  \"success\": %s\n}\n",
        operation_, utc_, result.preparation_error, result.acquisition_error,
        result.operation_started ? "true" : "false", result.operation_error,
        result.readback_attempted ? "true" : "false", result.readback_error,
        result.comparison_error, result.cleanup_error,
        result.success() ? "true" : "false");
    const int error = save_text(6, text, count);
    return error ? CardStatus{std::unexpected{static_cast<p2_error>(error)}}
                 : CardStatus{};
  }

private:
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
               Work work, void *context) {
  if (!directory || !utc || !operation || !work)
    return 20;
  auto journal = make_owned<CaptureJournal>(directory, utc, operation, true);
  if (!journal)
    return 20;
  const auto result = run(*journal, work, context);
  p2_fprintf(stderr,
             "Operation blocked: a verified timestamped full-flash backup is "
             "required.\n"
             "Physical profile and native transaction are not approved; "
             "no card access was attempted.\n");
  return result.success() ? 0 : 20;
}
#endif
} // namespace p2::operation
