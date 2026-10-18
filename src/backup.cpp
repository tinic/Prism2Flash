/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "file.h"
#include "p2_stdio.h"
#include "platform.h"
#include "sha256.h"
#include "storage.h"
#include "version.h"
#include <cstring>
#ifdef PRISM2_AMIGA
#define P2_CAPTURE_SIMULATION "false"
#else
#define P2_CAPTURE_SIMULATION "true"
#endif
namespace p2::backup {
bool valid_utc(std::string_view timestamp) {
  constexpr std::array<unsigned, 12> month_days{31, 28, 31, 30, 31, 30,
                                                31, 31, 30, 31, 30, 31};
  if (timestamp.size() != 16 || timestamp[8] != 'T' || timestamp[15] != 'Z')
    return false;
  for (size_t index = 0; index < 15; ++index)
    if (index != 8 && (timestamp[index] < '0' || timestamp[index] > '9'))
      return false;
  const unsigned year = (timestamp[0] - '0') * 1000 +
                        (timestamp[1] - '0') * 100 + (timestamp[2] - '0') * 10 +
                        timestamp[3] - '0';
  const unsigned month = (timestamp[4] - '0') * 10 + timestamp[5] - '0';
  const unsigned day = (timestamp[6] - '0') * 10 + timestamp[7] - '0';
  const unsigned hour = (timestamp[9] - '0') * 10 + timestamp[10] - '0';
  const unsigned minute = (timestamp[11] - '0') * 10 + timestamp[12] - '0';
  const unsigned second = (timestamp[13] - '0') * 10 + timestamp[14] - '0';
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || hour > 23 ||
      minute > 59 || second > 59)
    return false;
  const bool leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const unsigned days = month_days[month - 1] + (month == 2 && leap_year);
  return day >= 1 && day <= days;
}

namespace {
class NvCapture {
public:
  NvCapture(p2_bus &bus, FILE &first, FILE &second)
      : bus_(bus), card_(bus), files_{&first, &second} {}
  NvCapture(p2_bus &bus, FILE &first, uint32_t alias_bytes)
      : bus_(bus), card_(bus), files_{&first, nullptr},
        alias_bytes_(alias_bytes), passes_(1) {}
  CaptureReport run() {
    if (files_[0] == files_[1]) {
      report_.error = P2_E_ARGUMENT;
      return report_;
    }
    buffers_ = ByteBuffer::allocate(8192);
    if (!buffers_) {
      report_.error = P2_E_IO;
      return report_;
    }
    if (passes_ == 1) {
      if (!alias_bytes_ || alias_bytes_ % 4096 || NvBytes % alias_bytes_) {
        report_.error = P2_E_ARGUMENT;
        return report_;
      }
      canonical_ = ByteBuffer::allocate(alias_bytes_);
      if (!canonical_) {
        report_.error = P2_E_IO;
        return report_;
      }
    }
    const auto acquisition = acquire();
    const auto cleanup = acquired_ ? finish() : CardStatus{};
    report_.cleanup_error = cleanup ? P2_OK : cleanup.error();
    report_.error = acquisition ? static_cast<p2_error>(report_.cleanup_error)
                                : acquisition.error();
    if (acquired_)
      report_.elapsed_us = bus_.now_us(bus_.ctx) - start_;
    return report_;
  }

private:
  CardStatus available() const {
    if (!bus_.present(bus_.ctx))
      return std::unexpected{P2_E_REMOVED};
    if (bus_.cancelled && bus_.cancelled(bus_.ctx))
      return std::unexpected{P2_E_CANCELLED};
    return {};
  }
  CardStatus within_deadline() const {
    if (static_cast<uint32_t>(bus_.now_us(bus_.ctx) - start_) >= 240000000)
      return std::unexpected{P2_E_TIMEOUT};
    return {};
  }
  CardStatus stable_chunk(uint32_t address, std::span<uint8_t, 4096> out) {
    std::array<uint8_t, 128> next;
    for (std::size_t page = 0; page < out.size(); page += next.size()) {
      unsigned attempt = 0;
      for (; attempt < 20; ++attempt) {
        if (auto result = within_deadline(); !result)
          return result;
        if (attempt) {
          const auto control = card_.aux_control();
          if (!control)
            return std::unexpected{control.error()};
          if (!(*control & 0xc000)) {
            ++report_.aux_reenables;
            if (auto result = card_.enable_aux(true); !result)
              return result;
          }
        }
        auto bytes = attempt ? std::span{next} : out.subspan(page, next.size());
        if (auto result =
                card_.read_nv(address + static_cast<uint32_t>(page), bytes);
            !result)
          return result;
        ++report_.page_reads;
        if (attempt) {
          if (std::equal(next.begin(), next.end(), out.begin() + page))
            break;
          ++report_.page_disagreements;
          std::copy(next.begin(), next.end(), out.begin() + page);
          // Allow AUX data to settle after firmware commands. Only disagreeing
          // whole-page reads wait; no payload write, voting or byte repair.
          const bool startup = address == 0 && page == 0 && attempt >= 3;
          const auto delay =
              startup ? std::min(100000u * (attempt - 2), 500000u) : 5000u;
          bus_.delay_us(bus_.ctx, delay);
        }
      }
      // Two consecutive whole pages; never vote or repair individual bytes.
      if (attempt == 20) {
        p2_fprintf(stderr,
                   "NV page unstable at 0x%06lx after 20 reads; CONTROL=%04x\n",
                   static_cast<unsigned long>(address + page),
                   bus_.read16(bus_.ctx, P2_CONTROL));
        return std::unexpected{P2_E_VERIFY};
      }
    }
    return {};
  }
  CardStatus acquire() {
    if (auto result = available(); !result)
      return result;
    start_ = bus_.now_us(bus_.ctx);
    const auto control = card_.aux_control();
    if (!control)
      return std::unexpected{control.error()};
    control_ = *control;
    report_.before[0] = control_;
    if (auto result = available(); !result)
      return result;
    if (control_ == 0xffff ||
        ((control_ & 0xc000) != 0 && (control_ & 0xc000) != 0xc000))
      return std::unexpected{P2_E_IO};
    for (std::size_t i = 0; i < SavedRegisters.size(); ++i) {
      if (auto result = available(); !result)
        return result;
      report_.before[i + 1] = bus_.read16(bus_.ctx, SavedRegisters[i]);
    }
    if (auto result = available(); !result)
      return result;
    acquired_ = true;
    // Leave existing AUX enabled; disable only a session opened by this tool.
    if (!(control_ & 0xc000)) {
      opened_ = true;
      if (auto result = card_.enable_aux(true); !result)
        return result;
    }
    bool changed = false;
    auto chunk = std::span<uint8_t, 4096>{buffers_.bytes().data(), 4096};
    auto comparison = buffers_.bytes().subspan(4096);
    for (std::size_t pass = 0; pass < passes_; ++pass) {
      if (pass && (fflush(files_[0]) || fseek(files_[0], 0, SEEK_SET)))
        return std::unexpected{P2_E_IO};
      Sha256 hash;
      for (uint32_t address = 0; address < NvBytes; address += chunk.size()) {
        if (auto result = available(); !result)
          return result;
        if (auto result = within_deadline(); !result)
          return result;
        if (auto result = stable_chunk(address, chunk); !result)
          return result;
        if (pass) {
          const auto got =
              fread(comparison.data(), 1, comparison.size(), files_[0]);
          report_.comparison_read_bytes = static_cast<uint32_t>(got);
          if (got != comparison.size())
            return std::unexpected{P2_E_IO};
          for (std::size_t i = 0; i < chunk.size(); ++i)
            if (chunk[i] != comparison[i]) {
              if (!report_.mismatch_bytes)
                report_.first_mismatch = address + static_cast<uint32_t>(i);
              ++report_.mismatch_bytes;
            }
        }
        if (passes_ == 1) {
          if (address < alias_bytes_)
            std::copy(chunk.begin(), chunk.end(),
                      canonical_.bytes().begin() + address);
          else {
            const auto expected = canonical_.bytes().subspan(
                address % alias_bytes_, chunk.size());
            for (std::size_t i = 0; i < chunk.size(); ++i)
              if (chunk[i] != expected[i]) {
                if (!report_.mismatch_bytes)
                  report_.first_mismatch = address + static_cast<uint32_t>(i);
                ++report_.mismatch_bytes;
              }
          }
        }
        if (fwrite(chunk.data(), 1, chunk.size(), files_[pass]) != chunk.size())
          return std::unexpected{P2_E_IO};
        changed |= std::any_of(chunk.begin() + 1, chunk.end(),
                               [&](uint8_t byte) { return byte != chunk[0]; });
        hash.update(chunk);
        report_.bytes[pass] += chunk.size();
        if ((address & 0xfffff) == 0) {
          p2_printf("NV pass %u: %lu / %lu bytes\n",
                    static_cast<unsigned>(pass + 1),
                    static_cast<unsigned long>(report_.bytes[pass]),
                    static_cast<unsigned long>(NvBytes));
          fflush(stdout);
        }
      }
      if (fflush(files_[pass]))
        return std::unexpected{P2_E_IO};
      report_.sha256[pass] = hash.hex();
    }
    if (!changed || report_.mismatch_bytes ||
        (passes_ == 2 && report_.sha256[0] != report_.sha256[1]))
      return std::unexpected{P2_E_VERIFY};
    return {};
  }
  CardStatus finish() {
    // Ignore Ctrl-C only for cleanup. Presence is still sticky in the backend.
    auto cleanup_bus = bus_;
    cleanup_bus.cancelled = nullptr;
    Card cleanup_card{cleanup_bus};
    CardStatus result;
    // Clear selector while AUX is enabled; never replay opaque snapshots.
    if (bus_.present(bus_.ctx))
      bus_.write16(bus_.ctx, P2_AUXOFFSET, 0);
    if (opened_)
      result = cleanup_card.enable_aux(false);
    if (!bus_.present(bus_.ctx) && result)
      result = std::unexpected{P2_E_REMOVED};
    if (bus_.present(bus_.ctx)) {
      report_.after[0] = bus_.read16(bus_.ctx, P2_CONTROL);
      if ((report_.after[0] & 0xc000) != (control_ & 0xc000) && result)
        result = std::unexpected{P2_E_VERIFY};
      for (std::size_t i = 0;
           i < SavedRegisters.size() && bus_.present(bus_.ctx); ++i)
        report_.after[i + 1] = bus_.read16(bus_.ctx, SavedRegisters[i]);
      if (!bus_.present(bus_.ctx) && result)
        result = std::unexpected{P2_E_REMOVED};
    }
    return result;
  }
  static constexpr std::array<uint16_t, 5> SavedRegisters{
      P2_PARAM0, P2_PARAM1, P2_PARAM2, P2_AUXPAGE, P2_AUXOFFSET};
  p2_bus &bus_;
  Card card_;
  std::array<FILE *, 2> files_;
  ByteBuffer buffers_;
  ByteBuffer canonical_;
  uint32_t alias_bytes_ = 0;
  std::size_t passes_ = 2;
  CaptureReport report_;
  uint32_t start_ = 0;
  uint16_t control_ = 0;
  bool opened_ = false, acquired_ = false;
};
} // namespace
int verify_file(const char *name, const char *expected, uint32_t size) {
  auto file = File::open(name, "rb");
  auto buffer = ByteBuffer::allocate(4096);
  if (!file || !buffer)
    return P2_E_IO;
  Sha256 hash;
  uint32_t total = 0;
  while (const auto count =
             fread(buffer.bytes().data(), 1, buffer.size(), file.get())) {
    if (count > size - total)
      return P2_E_IO;
    total += static_cast<uint32_t>(count);
    hash.update(buffer.bytes().first(count));
  }
  if (ferror(file.get()) || total != size)
    return P2_E_IO;
  const auto actual = hash.hex();
  if (!file.close())
    return P2_E_IO;
  return std::strcmp(actual.data(), expected) ? P2_E_VERIFY : P2_OK;
}
CaptureReport capture(p2_bus &bus, FILE &first, FILE &second) {
  return NvCapture{bus, first, second}.run();
}
CaptureReport capture_single(p2_bus &bus, FILE &file, uint32_t alias_bytes) {
  return NvCapture{bus, file, alias_bytes}.run();
}
int run_command(const char *directory, const char *utc) {
  if (!valid_utc(utc) || !std::strstr(directory, utc) ||
      std::strlen(directory) > 320) {
    p2_fprintf(stderr, "Use a new persistent directory containing the supplied "
                       "YYYYMMDDTHHMMSSZ UTC timestamp.\n");
    return 20;
  }
  if (p2_platform_backup_directory(directory)) {
    p2_fprintf(stderr, "Cannot create a NEW backup directory; existing "
                       "directories and RAM/T are refused.\n");
    return 20;
  }
  std::array<std::array<char, 384>, 3> paths;
  constexpr std::array names{"nv-read-1.bin", "nv-read-2.bin", "manifest.json"};
  for (std::size_t i = 0; i < paths.size(); ++i)
    p2_snprintf(paths[i].data(), paths[i].size(), "%s/%s", directory, names[i]);
  std::array files{File::open(paths[0].data(), "w+b"),
                   File::open(paths[1].data(), "w+b")};
  CaptureReport result;
  p2_card_info card{};
  PlatformError error;
  int rc = files[0] && files[1] ? P2_OK : P2_E_IO;
  // The lambda bounds slot ownership before files are closed and re-opened.
  if (!rc) {
    rc = [&]() -> int {
      auto slot = Slot::open();
      if (!slot) {
        error = slot.error();
        return error.code;
      }
      card = slot->info();
      auto bus = slot->configure(true);
      if (!bus) {
        error = bus.error();
        return error.code;
      }
      result = capture(*bus, *files[0], *files[1]);
      result.backend_cleanup_error = slot->finish_backup();
      return result.error ? result.error : result.backend_cleanup_error;
    }();
  }
  for (auto &file : files)
    if (!file.close() && !rc)
      rc = P2_E_IO;
  bool verify = false;
  if (result.bytes[0] == NvBytes && result.bytes[1] == NvBytes) {
    int file_rc = verify_file(paths[0].data(), result.sha256[0].data());
    if (!file_rc)
      file_rc = verify_file(paths[1].data(), result.sha256[1].data());
    if (!file_rc)
      verify = true;
    else if (!rc)
      rc = file_rc;
  }
  auto manifest = File::open(paths[2].data(), "wb");
  if (manifest) {
    p2_fprintf(manifest.get(),
               "{\n  \"format\": \"Prism2 AUX NV capture 1\",\n  "
               "\"tool_version\": \"" P2_VERSION "\",\n"
               "  \"utc_supplied\": \"%s\",\n  \"timestamp_basis\": "
               "\"operator-supplied UTC; host must retain actual start/end and "
               "build-info.json\",\n"
               "  \"simulation_only\": " P2_CAPTURE_SIMULATION ",\n"
               "  \"cis_manufacturer\": %u,\n  \"cis_product\": %u,\n  "
               "\"space\": \"NV\",\n  \"selector\": 1,\n"
               "  \"address_start\": 0,\n  \"address_end_exclusive\": "
               "8388608,\n  \"byte_order\": \"little-endian word bytes\",\n"
               "  \"bytes_read\": [%lu, %lu],\n  \"sha256\": [\"%s\", "
               "\"%s\"],\n  \"elapsed_us\": %lu,\n"
               "  \"cleanup_error\": %d,\n  \"error\": %d,\n  "
               "\"nv_address_space_complete\": %s,\n"
               "  \"backend_cleanup_error\": %d,\n"
               "  \"page_verification\": \"two consecutive identical 128-byte "
               "reads; maximum twenty reads per page per pass\",\n"
               "  \"page_reads\": %lu,\n  \"page_disagreements\": %lu,\n"
               "  \"mismatch_bytes\": %lu,\n  \"first_mismatch\": %lu,\n  "
               "\"comparison_read_bytes\": %lu,\n"
               "  \"register_order\": "
               "[\"CONTROL\",\"PARAM0\",\"PARAM1\",\"PARAM2\",\"AUXPAGE\","
               "\"AUXOFFSET\"],\n"
               "  \"registers_before\": [%u,%u,%u,%u,%u,%u],\n  "
               "\"registers_after\": [%u,%u,%u,%u,%u,%u],\n"
               "  \"files_reopened_and_verified\": %s,\n  "
               "\"physical_flash_coverage_verified\": false,\n"
               "  \"authorizes_impactful_operation\": false\n}\n",
               utc, card.manufacturer, card.product,
               (unsigned long)result.bytes[0], (unsigned long)result.bytes[1],
               result.sha256[0].data(), result.sha256[1].data(),
               (unsigned long)result.elapsed_us, result.cleanup_error, rc,
               rc ? "false" : "true", result.backend_cleanup_error,
               (unsigned long)result.page_reads,
               (unsigned long)result.page_disagreements,
               (unsigned long)result.mismatch_bytes,
               (unsigned long)result.first_mismatch,
               (unsigned long)result.comparison_read_bytes, result.before[0],
               result.before[1], result.before[2], result.before[3],
               result.before[4], result.before[5], result.after[0],
               result.after[1], result.after[2], result.after[3],
               result.after[4], result.after[5], verify ? "true" : "false");
    if (ferror(manifest.get()))
      rc = P2_E_IO;
    if (!manifest.close())
      rc = P2_E_IO;
  } else
    rc = P2_E_IO;
  if (rc)
    p2_fprintf(stderr,
               "NV capture failed: %s; %s. Keep partial files and manifest.\n",
               p2_error_string(rc), error.message.data());
  else
    p2_printf("Two complete AUX NV reads match byte-for-byte; saved files "
              "reopened and SHA-256 verified.\n"
              "SHA-256: %s\nPhysical flash coverage/profile must still be "
              "established; active operations remain blocked.\n",
              result.sha256[0].data());
  return rc ? 20 : 0;
}
} // namespace p2::backup
