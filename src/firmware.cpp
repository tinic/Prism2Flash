/* SPDX-License-Identifier: GPL-2.0-only
 * Strict, bounded byte-oriented reader. Metadata semantics adapted from
 * Jouni Malinen's GPL-2.0 Host AP utilities 0.4.7 prism2_srec.c.
 */
#include "firmware.h"
#include <algorithm>
#include <cstring>

namespace p2::firmware {
namespace {
constexpr uint32_t InfoBase = 0xff200000;
constexpr std::size_t InfoLimit = 4096;
uint16_t le16(const uint8_t *p) {
  return p[0] | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t le32(const uint8_t *p) {
  return p[0] | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
p2::Identity identity(const uint8_t *p) {
  return {le16(p), le16(p + 2), le16(p + 4), le16(p + 6)};
}
int hex(uint8_t c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace

class Parser {
public:
  // State lives on the heap: the information record alone exceeds the normal
  // 4 KiB Amiga shell stack. The input cursor itself needs no allocation.
  template <typename Next> static Result<Image> run(Next next) {
    auto parser = std::unique_ptr<Parser>{new (std::nothrow) Parser};
    if (!parser)
      return std::unexpected{
          Error{Errc::Memory, 0, "Out of memory for parser"}};
    return parser->consume(next);
  }

private:
  std::unexpected<Error> fail(Errc code, const char *message) const {
    return std::unexpected{Error{code, line_, message}};
  }
  template <typename T, std::size_t Limit>
  Status append(BoundedVector<T, Limit> &items, T value, const char *limit) {
    if (items.size() == Limit)
      return fail(Errc::Limit, limit);
    if (!items.push_back(std::move(value)))
      return fail(Errc::Memory, "Out of memory for firmware records");
    return {};
  }
  Status info_record() {
    const auto *p = info_.data() + 4;
    const auto length = info_used_ - 4;
    switch (le16(info_.data() + 2)) {
    case 1:
      if (length != 8 || component_seen_)
        return fail(Errc::Format, "Invalid or duplicate component identity");
      image_.component_ = identity(p);
      component_seen_ = true;
      break;
    case 2:
      if (length < 10 || (length - 4) % 6 || le16(p) > 1 || !le16(p + 2) ||
          le16(p + 2) > 4)
        return fail(Errc::Format, "Invalid compatibility record");
      for (std::size_t i = 4; i < length; i += 6) {
        const InterfaceRange range{le16(p), le16(p + 2), le16(p + i),
                                   le16(p + i + 2), le16(p + i + 4)};
        if (range.bottom > range.top)
          return fail(Errc::Format, "Reversed compatibility interval");
        if (auto result =
                append(image_.ranges_, range, "Too many interface ranges");
            !result)
          return result;
      }
      break;
    case 3:
      if (length != 2 || image_.sequence_)
        return fail(Errc::Format, "Invalid or duplicate build sequence");
      image_.sequence_ = le16(p);
      break;
    case 4:
      if (length != 8)
        return fail(Errc::Format, "Invalid platform identity");
      if (auto result = append(image_.platforms_, identity(p),
                               "Too many platform identities");
          !result)
        return result;
      break;
    case 0x8001:
      if (name_seen_ || !length || length >= image_.name_.size())
        return fail(Errc::Format, "Invalid or duplicate firmware name");
      name_seen_ = true;
      for (std::size_t i = 0; i < length; ++i) {
        if (p[i] && (p[i] < 32 || p[i] > 126))
          return fail(Errc::Format, "Non-ASCII firmware name");
        image_.name_[i] = static_cast<char>(p[i]);
      }
      break;
    default:
      return fail(Errc::Format, "Unknown firmware information type");
    }
    info_used_ = info_expected_ = 0;
    return {};
  }
  Status info_fragment(uint32_t address, std::span<const uint8_t> bytes) {
    if (address != InfoBase + info_used_ || bytes.empty())
      return fail(Errc::Format, "Discontinuous firmware information fragments");
    if (!info_used_) {
      if (bytes.size() < 4 || !le16(bytes.data()))
        return fail(Errc::Format, "Truncated firmware information header");
      info_expected_ = static_cast<std::size_t>(le16(bytes.data())) * 2 + 2;
      // Names count payload words; other types count type+payload.
      if (le16(bytes.data() + 2) == 0x8001)
        info_expected_ += 2;
      if (info_expected_ < 4 || info_expected_ > InfoLimit)
        return fail(Errc::Limit, "Firmware information exceeds limit");
    }
    if (bytes.size() > info_expected_ - info_used_)
      return fail(Errc::Format, "Firmware information length mismatch");
    std::copy(bytes.begin(), bytes.end(), info_.begin() + info_used_);
    info_used_ += bytes.size();
    return info_used_ == info_expected_ ? info_record() : Status{};
  }
  Status payload(uint32_t address, std::span<const uint8_t> bytes) {
    if (bytes.empty() || address >= 0x01000000 ||
        bytes.size() > 0x01000000 - address)
      return fail(Errc::Format, "Empty or overflowing payload address range");
    if (bytes.size() > MaxPayloadBytes - image_.payload_bytes_ ||
        image_.segments_.size() == MaxSegments)
      return fail(Errc::Limit, "Payload or segment limit exceeded");
    auto data = ByteBuffer::allocate(bytes.size());
    if (!data)
      return fail(Errc::Memory, "Out of memory for payload");
    std::copy(bytes.begin(), bytes.end(), data.bytes().begin());
    if (auto result =
            append(image_.segments_, Segment{address, std::move(data)},
                   "Too many segments");
        !result)
      return result;
    image_.payload_bytes_ += bytes.size();
    return {};
  }
  Status s3(uint32_t address, std::span<const uint8_t> bytes) {
    if (info_used_ && (address & 0xffff0000) != InfoBase)
      return fail(Errc::Format, "Unfinished firmware information record");
    if (address == 0xff000000) {
      if (bytes.size() != 12)
        return fail(Errc::Format, "Invalid PDA plug record length");
      const auto *p = bytes.data();
      const Plug plug{le32(p), le32(p + 4), le32(p + 8)};
      if ((!plug.pdr || plug.pdr > 0xffff) && plug.pdr != UINT32_MAX)
        return fail(Errc::Format, "Invalid plug PDR identifier");
      if (!plug.length || plug.address >= 0x01000000 ||
          plug.length > 0x01000000 - plug.address)
        return fail(Errc::Format, "Invalid plug address range");
      return append(image_.plugs_, plug, "Too many plug records");
    }
    if (address == 0xff100000) {
      if (bytes.size() != 12)
        return fail(Errc::Format, "Invalid signature descriptor length");
      const auto *p = bytes.data();
      const Signature signature{le32(p), le32(p + 4), le32(p + 8)};
      if (signature.program > 1 || !signature.length ||
          signature.start >= 0x01000000 ||
          signature.length > 0x01000000 - signature.start ||
          (signature.program && signature.start < 2))
        return fail(Errc::Format, "Invalid signature descriptor range or flag");
      return append(image_.signatures_, signature,
                    "Too many signature descriptors");
    }
    if ((address & 0xffff0000) == InfoBase)
      return info_fragment(address, bytes);
    if (address & 0xff000000)
      return fail(Errc::Format, "Unknown special S3 address");
    return payload(address, bytes);
  }
  Status record(std::span<const uint8_t> line) {
    std::array<uint8_t, 256> bytes{};
    if (line.size() < 4 || line[0] != 'S' || (line.size() & 1))
      return fail(Errc::Format, "Malformed S-record line");
    const auto n = (line.size() - 2) / 2;
    if (n > bytes.size())
      return fail(Errc::Limit, "S-record line too long");
    unsigned sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const auto hi = hex(line[2 + i * 2]), lo = hex(line[3 + i * 2]);
      if (hi < 0 || lo < 0)
        return fail(Errc::Format, "Non-hexadecimal S-record byte");
      bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
      sum += bytes[i];
    }
    if (static_cast<std::size_t>(bytes[0]) + 1 != n || (sum & 255) != 255)
      return fail(Errc::Format, "S-record length or checksum mismatch");
    const auto type = line[1];
    std::size_t address_length;
    switch (type) {
    case '0':
    case '5':
      address_length = 2;
      break;
    case '6':
      address_length = 3;
      break;
    case '3':
    case '7':
      address_length = 4;
      break;
    default:
      return fail(Errc::Format, "Unsupported S-record type");
    }
    if (n < address_length + 2)
      return fail(Errc::Format, "S-record is shorter than its address");
    uint32_t address = 0;
    for (std::size_t i = 0; i < address_length; ++i)
      address = (address << 8) | bytes[1 + i];
    if (type == '0') {
      if (header_seen_ || image_.records_ || entry_seen_ || address)
        return fail(Errc::Format, "Misplaced S0 header");
      header_seen_ = true;
      return {};
    }
    if (type == '3') {
      if (count_seen_)
        return fail(Errc::Format, "Data after record count");
      ++image_.records_;
      return s3(address, std::span{bytes}.subspan(5, n - 6));
    }
    if (info_used_)
      return fail(Errc::Format, "Unfinished firmware information record");
    if (n != address_length + 2)
      return fail(Errc::Format, "Unexpected control record payload");
    // Intersil S7 supplies a unique entry, not EOF: it may precede S3.
    if (type == '7') {
      if (entry_seen_)
        return fail(Errc::Format, "Duplicate S7 entry record");
      image_.entry_ = address;
      entry_seen_ = true;
      return {};
    }
    if (count_seen_ || address != image_.records_)
      return fail(Errc::Format, "Invalid or duplicate record count");
    count_seen_ = true;
    return {};
  }
  Status combine() {
    auto &segments = image_.segments_;
    std::sort(segments.begin(), segments.end(),
              [](const Segment &a, const Segment &b) {
                return a.address < b.address;
              });
    for (std::size_t i = 1; i < segments.size(); ++i)
      if (segments[i].address <
          segments[i - 1].address + segments[i - 1].data.size())
        return fail(Errc::Format, "Overlapping payload records");
    std::size_t used = 0;
    for (std::size_t i = 0; i < segments.size();) {
      auto j = i + 1;
      auto total = segments[i].data.size();
      while (j < segments.size() &&
             segments[j].address == segments[i].address + total)
        total += segments[j++].data.size();
      if (j == i + 1) {
        if (used != i)
          segments[used] = std::move(segments[i]);
        ++used;
      } else {
        auto data = ByteBuffer::allocate(total);
        if (!data)
          return fail(Errc::Memory, "Out of memory combining segments");
        auto destination = data.bytes().begin();
        for (auto k = i; k < j; ++k) {
          auto bytes = segments[k].data.bytes();
          destination = std::copy(bytes.begin(), bytes.end(), destination);
        }
        const auto address = segments[i].address;
        segments[used++] = Segment{address, std::move(data)};
      }
      i = j;
    }
    // Each buffer always has one owner, even if allocation failed mid-merge.
    segments.truncate(used);
    return {};
  }
  template <typename Next> Result<Image> consume(Next next) {
    std::array<uint8_t, 515> line{};
    std::size_t length = 0;
    unsigned long bytes = 0;
    for (int c; (c = next()) != EOF;) {
      if (++bytes > MaxInputBytes)
        return fail(Errc::Limit, "Input byte limit exceeded");
      if (c == '\n') {
        if (++line_ > MaxLines)
          return fail(Errc::Limit, "Input line limit exceeded");
        if (length && line[length - 1] == '\r')
          --length;
        if (length) {
          if (auto result = record(std::span{line}.first(length)); !result)
            return std::unexpected{result.error()};
        }
        length = 0;
      } else {
        if (length == line.size())
          return fail(Errc::Limit, "S-record line too long");
        line[length++] = static_cast<uint8_t>(c);
      }
    }
    if (length) {
      ++line_;
      if (line[length - 1] == '\r')
        --length;
      if (length) {
        if (auto result = record(std::span{line}.first(length)); !result)
          return std::unexpected{result.error()};
      }
    }
    if (!entry_seen_ || !component_seen_ || image_.segments_.empty() ||
        info_used_)
      return fail(Errc::Format,
                  "Missing S7, component, payload or complete metadata");
    if (auto result = combine(); !result)
      return std::unexpected{result.error()};
    return std::move(image_);
  }
  Image image_;
  std::array<uint8_t, InfoLimit> info_{};
  std::size_t info_used_ = 0, info_expected_ = 0;
  unsigned long line_ = 0;
  bool component_seen_ = false, entry_seen_ = false, header_seen_ = false;
  bool count_seen_ = false, name_seen_ = false;
};

Result<Image> Image::parse(std::span<const uint8_t> input) {
  return Parser::run([input]() mutable -> int {
    if (input.empty())
      return EOF;
    const auto byte = input.front();
    input = input.subspan(1);
    return byte;
  });
}
Result<Image> Image::read(FILE &file) {
  auto result = Parser::run([&file] { return fgetc(&file); });
  if (ferror(&file))
    return std::unexpected{Error{Errc::Io, 0, "Input read error"}};
  return result;
}
bool Image::contains(uint32_t start, uint32_t length) const {
  return std::any_of(segments_.begin(), segments_.end(), [=](const Segment &s) {
    return s.contains(start, length);
  });
}
} // namespace p2::firmware
