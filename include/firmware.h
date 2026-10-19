/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "prism2.h"
#include "storage.h"
#include <array>
#include <expected>
#include <optional>
#include <stdio.h>
#include <string_view>

namespace p2::firmware {
inline constexpr std::size_t MaxItems = 256;
inline constexpr std::size_t MaxSignatures = 64;
inline constexpr std::size_t MaxSegments = 8192;
inline constexpr std::size_t MaxPayloadBytes = 1024 * 1024;
inline constexpr unsigned long MaxInputBytes = 8ul * 1024 * 1024;
inline constexpr unsigned long MaxLines = 65536;

enum class Mode { Ram, Flash };
enum class PlugPolicy { RequireAll, DosOptionalDefaults };
struct PlugFallback {
  uint32_t pdr = 0, expected_bytes = 0, actual_bytes = 0;
  bool missing = false;
  bool target_absent = false;
};
enum class Errc { Format, Limit, Memory, Io, Incompatible, Pda };
struct Error {
  Errc code;
  unsigned long line = 0;
  const char *message;
};
template <typename T> using Result = std::expected<T, Error>;
using Status = Result<void>;

struct InterfaceRange {
  uint16_t role, interface_id, variant, bottom, top;
  bool operator==(const InterfaceRange &) const = default;
};
struct Plug {
  uint32_t pdr, address, length;
};
// Host AP calls these CRC records but stamps DE C0; no image CRC is computed.
struct Signature {
  uint32_t start, length, program;
};
struct Segment {
  uint32_t address = 0;
  ByteBuffer data;
  [[nodiscard]] bool contains(uint32_t start, uint32_t length) const {
    return start >= address && start - address <= data.size() &&
           length <= data.size() - (start - address);
  }
};
struct CardSnapshot {
  p2::Identity nic{};
  std::optional<std::array<uint8_t, 6>> mac;
  std::array<std::optional<InterfaceRange>, 4> suppliers{};
  [[nodiscard]] static Result<CardSnapshot> read(FILE &file);
  [[nodiscard]] static Result<CardSnapshot> from_pda(std::span<const uint8_t>);
};
class Parser;
class InitialImage;
// A successfully parsed image owns its payload and exposes immutable views.
// Moving transfers ownership; there is no partially initialized public image.
class Image {
public:
  Image(Image &&) = default;
  Image &operator=(Image &&) = default;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  [[nodiscard]] static Result<Image> parse(std::span<const uint8_t> input);
  [[nodiscard]] static Result<Image> read(FILE &file);
  [[nodiscard]] Status validate(Mode mode, const CardSnapshot &card,
                                std::span<const uint8_t> pda,
                                const Image *primary = nullptr,
                                PlugPolicy = PlugPolicy::RequireAll) const;
  [[nodiscard]] const p2::Identity &component() const { return component_; }
  [[nodiscard]] uint32_t entry() const { return entry_; }
  [[nodiscard]] unsigned long records() const { return records_; }
  [[nodiscard]] std::size_t payload_bytes() const { return payload_bytes_; }
  [[nodiscard]] auto segments() const { return segments_.view(); }
  [[nodiscard]] auto platforms() const { return platforms_.view(); }
  [[nodiscard]] auto ranges() const { return ranges_.view(); }
  [[nodiscard]] auto plugs() const { return plugs_.view(); }
  [[nodiscard]] auto signatures() const { return signatures_.view(); }
  [[nodiscard]] std::string_view name() const { return name_.data(); }
  [[nodiscard]] std::string_view source_sha256() const {
    return source_sha256_.data();
  }
  [[nodiscard]] std::optional<uint16_t> build_sequence() const {
    return sequence_;
  }
  [[nodiscard]] bool contains(uint32_t start, uint32_t length) const;

private:
  friend class Parser;
  friend class InitialImage;
  Image() = default;
  p2::Identity component_{};
  uint32_t entry_ = 0;
  unsigned long records_ = 0;
  std::size_t payload_bytes_ = 0;
  BoundedVector<Segment, MaxSegments> segments_;
  BoundedVector<p2::Identity, MaxItems> platforms_;
  BoundedVector<InterfaceRange, MaxItems> ranges_;
  BoundedVector<Plug, MaxItems> plugs_;
  BoundedVector<Signature, MaxSignatures> signatures_;
  std::array<char, 128> name_{};
  std::array<char, 65> source_sha256_{};
  std::optional<uint16_t> sequence_;
};

// DOS -i images are raw RAM bootstraps: they have no component metadata and
// can have S7=0. Keep them distinct from PRI/STA images; never classify them
// as flash or feed them to PDA plugging/signature preparation.
class InitialImage {
public:
  InitialImage(InitialImage &&) = default;
  InitialImage &operator=(InitialImage &&) = default;
  InitialImage(const InitialImage &) = delete;
  InitialImage &operator=(const InitialImage &) = delete;
  [[nodiscard]] static Result<InitialImage> parse(std::span<const uint8_t>);
  [[nodiscard]] static Result<InitialImage> read(FILE &);
  [[nodiscard]] auto segments() const { return image_.segments(); }
  [[nodiscard]] auto source_sha256() const { return image_.source_sha256(); }
  [[nodiscard]] auto payload_bytes() const { return image_.payload_bytes(); }
  [[nodiscard]] auto records() const { return image_.records(); }

private:
  explicit InitialImage(Image image) noexcept : image_(std::move(image)) {}
  Image image_;
};

// The filename plug uses the source basename, not embedded firmware metadata.
struct ImageSource {
  const Image &image;
  std::string_view basename;
};
// Preparation is transactional and has no card access. A prepared value owns
// private payload copies; it is not approval of a physical memory map or
// backup.
class PreparedImage {
public:
  PreparedImage(PreparedImage &&) = default;
  PreparedImage &operator=(PreparedImage &&) = default;
  PreparedImage(const PreparedImage &) = delete;
  PreparedImage &operator=(const PreparedImage &) = delete;
  [[nodiscard]] static Result<PreparedImage>
  create(ImageSource target, Mode mode, const CardSnapshot &card,
         std::span<const uint8_t> pda, std::optional<ImageSource> primary = {},
         PlugPolicy = PlugPolicy::RequireAll);
  [[nodiscard]] auto segments() const { return segments_.view(); }
  [[nodiscard]] uint32_t entry() const { return entry_; }
  [[nodiscard]] std::size_t payload_bytes() const { return payload_bytes_; }
  [[nodiscard]] auto fallbacks() const { return fallbacks_.view(); }

private:
  PreparedImage() = default;
  BoundedVector<Segment, MaxSegments * 2> segments_;
  uint32_t entry_ = 0;
  std::size_t payload_bytes_ = 0;
  BoundedVector<PlugFallback, MaxItems * 2> fallbacks_;
};
} // namespace p2::firmware
