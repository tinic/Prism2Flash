/* SPDX-License-Identifier: GPL-2.0-only
 * Compatibility/PDA semantics adapted from Host AP utilities 0.4.7.
 * Deliberately no force option, default calibration fallback or hardware I/O.
 */
#include "firmware.h"
#include <algorithm>
#include <charconv>
#include <cstring>

namespace p2::firmware {
namespace {
std::unexpected<Error> fail(Errc code, const char *message) {
  return std::unexpected{Error{code, 0, message}};
}
uint16_t le16(const uint8_t *p) {
  return p[0] | (static_cast<uint16_t>(p[1]) << 8);
}
// Construct only after CRC and structural validation. Views borrow the PDA.
class PdaView {
public:
  static Result<PdaView> checked(std::span<const uint8_t> bytes) {
    if (bytes.size() != P2_PDA_SIZE || p2_validate_pda(bytes.data()))
      return fail(Errc::Pda,
                  "PDA must be a valid CRC-checked 1024-byte binary backup");
    std::array<uint16_t, 256> seen{};
    std::size_t count = 0;
    for (std::size_t pos = 0; pos + 4 <= bytes.size();) {
      const auto id = le16(bytes.data() + pos + 2);
      if (!id)
        break;
      if (std::find(seen.begin(), seen.begin() + count, id) !=
          seen.begin() + count)
        return fail(Errc::Pda, "Duplicate PDR in PDA backup");
      if (count == seen.size())
        return fail(Errc::Pda, "Too many PDRs");
      seen[count++] = id;
      pos += (le16(bytes.data() + pos) + 1) * 2;
    }
    return PdaView{bytes};
  }
  std::optional<std::span<const uint8_t>> find(uint16_t type) const {
    for (std::size_t pos = 0; pos + 4 <= bytes_.size();) {
      const auto words = le16(bytes_.data() + pos);
      const auto id = le16(bytes_.data() + pos + 2);
      if (!id)
        break;
      if (id == type)
        return bytes_.subspan(pos + 4, (words - 1) * 2);
      pos += (words + 1) * 2;
    }
    return std::nullopt;
  }
  Result<CardSnapshot> bind(CardSnapshot card) const {
    const auto value = find(8);
    if (!value || value->size() != 8)
      return fail(Errc::Pda, "PDA has no correctly sized NIC identity PDR");
    const auto *p = value->data();
    const p2::Identity nic{le16(p), le16(p + 2), le16(p + 4), le16(p + 6)};
    if (nic != card.nic)
      return fail(Errc::Incompatible,
                  "PDA NIC identity differs from saved card identity");
    const auto mac = find(0x101);
    if (!card.mac || !mac || mac->size() != 6 ||
        !std::equal(mac->begin(), mac->end(), card.mac->begin()))
      return fail(
          Errc::Incompatible,
          "PDA MAC address differs from or is absent in saved card data");
    for (std::size_t i = 0; i < 2; ++i) {
      const auto supplier = find(static_cast<uint16_t>(6 + i));
      if (!supplier || supplier->size() != 10)
        return fail(Errc::Pda, "PDA lacks RF/MAC supplier range records");
      const auto *p = supplier->data();
      const InterfaceRange range{le16(p), le16(p + 2), le16(p + 4), le16(p + 6),
                                 le16(p + 8)};
      if (range.role || range.interface_id != i + 1 || range.bottom > range.top)
        return fail(Errc::Pda, "PDA RF/MAC supplier range is malformed");
      if (card.suppliers[i] && range != *card.suppliers[i])
        return fail(Errc::Incompatible,
                    "Saved supplier range differs from PDA");
      card.suppliers[i] = range;
    }
    return card;
  }

private:
  explicit PdaView(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  std::span<const uint8_t> bytes_;
};
// DOS warns and retains the HEX bytes for missing/short PDRs. Restrict that
// behavior to known optional configuration fields; identity, regulatory and
// RF calibration records remain mandatory and byte-exact.
bool optional_default(uint32_t pdr, PlugPolicy policy) {
  constexpr std::array<uint32_t, 12> optional{
      1, 3, 5, 0x402, 0x403, 0x404, 0x408, 0x405, 0x406, 0x302, 0x303, 0x412};
  return policy == PlugPolicy::DosOptionalDefaults &&
         std::find(optional.begin(), optional.end(), pdr) != optional.end();
}
Status check_image(const Image &image, Mode mode, const CardSnapshot &card,
                   const PdaView &pda, PlugPolicy policy) {
  if (image.component().id != 0x15 && image.component().id != 0x1f)
    return fail(Errc::Incompatible,
                "Only PRI and STA components are supported for validation");
  if ((mode == Mode::Flash && image.entry()) ||
      (mode == Mode::Ram && !image.entry()))
    return fail(Errc::Incompatible,
                "Image RAM/flash class differs from selected mode");
  if (std::none_of(image.platforms().begin(), image.platforms().end(),
                   [&](const auto &id) { return id == card.nic; }))
    return fail(
        Errc::Incompatible,
        "Exact NIC id/variant/major/minor is absent from image platforms");
  for (const auto &segment : image.segments())
    if ((segment.address & 1) || (segment.data.size() & 1) ||
        segment.address >= 0x800000 ||
        segment.data.size() > 0x800000 - segment.address)
      return fail(
          Errc::Incompatible,
          "Payload exceeds current even-aligned 23-bit AUX transport bounds");
  unsigned actors = 0;
  // Host AP's conservative per-record checks, including replacement PRI+STA.
  for (const auto &actor : image.ranges()) {
    if (!actor.role)
      continue;
    ++actors;
    const auto &supplier = card.suppliers[actor.interface_id - 1];
    if (!supplier)
      return fail(Errc::Incompatible,
                  "Saved card data lacks a required supplier interface");
    if (actor.variant != supplier->variant || actor.top < supplier->bottom ||
        actor.bottom > supplier->top)
      return fail(Errc::Incompatible,
                  "Actor/supplier interface variant or range mismatch");
  }
  if (!actors)
    return fail(Errc::Incompatible, "Image has no actor compatibility records");
  for (std::size_t i = 0; i < image.plugs().size(); ++i) {
    const auto &plug = image.plugs()[i];
    if (!image.contains(plug.address, plug.length))
      return fail(Errc::Incompatible,
                  "PDA plug destination is outside image payload");
    const auto inactive = [&](const Plug &candidate) {
      const auto value = pda.find(static_cast<uint16_t>(candidate.pdr));
      return optional_default(candidate.pdr, policy) &&
             (!value || value->size() != candidate.length);
    };
    if (inactive(plug))
      continue; // A retained image default performs no destination write.
    for (const auto &other : image.plugs().first(i)) {
      if (inactive(other))
        continue;
      if (plug.address < other.address + other.length &&
          other.address < plug.address + plug.length)
        return fail(Errc::Incompatible, "Overlapping PDA plug destinations");
    }
    if (plug.pdr == UINT32_MAX)
      continue; // Reserved filename field, not card calibration.
    const auto value = pda.find(static_cast<uint16_t>(plug.pdr));
    if ((!value || value->size() != plug.length) &&
        optional_default(plug.pdr, policy))
      continue;
    if (!value)
      return fail(Errc::Pda,
                  "Required plug PDR is missing; defaults are not accepted");
    if (value->size() != plug.length)
      return fail(Errc::Pda, "PDA plug length differs from image requirement");
  }
  for (const auto &signature : image.signatures()) {
    if ((signature.start & 1) || (signature.length & 1) ||
        signature.start >= 0x800000 ||
        signature.length > 0x800000 - signature.start)
      return fail(Errc::Incompatible,
                  "Signature descriptor exceeds AUX bounds");
    if (!image.contains(signature.start, 1) ||
        !image.contains(signature.start + signature.length - 1, 1))
      return fail(Errc::Incompatible,
                  "Signature descriptor endpoints are outside payload");
    if (signature.program) {
      if (std::none_of(
              image.segments().begin(), image.segments().end(),
              [&](const auto &s) { return s.address == signature.start; }))
        return fail(Errc::Incompatible,
                    "Signature region does not begin at a payload segment");
      for (const auto &plug : image.plugs())
        if (plug.address < signature.start &&
            plug.address + plug.length > signature.start - 2)
          return fail(Errc::Incompatible,
                      "Signature stamp would overwrite a PDA plug");
    }
  }
  return {};
}

class SnapshotParser {
public:
  Result<CardSnapshot> read(FILE &file) {
    std::array<char, 256> line{};
    std::size_t used = 0;
    for (int c; (c = fgetc(&file)) != EOF;) {
      if (c == 0 || used == line.size() - 1)
        return bad();
      line[used++] = static_cast<char>(c);
      if (c == '\n') {
        if (auto result = consume({line.data(), used}); !result)
          return std::unexpected{result.error()};
        used = 0;
      }
    }
    if (ferror(&file))
      return fail(Errc::Io, "Card snapshot read error");
    if (used) {
      if (auto result = consume({line.data(), used}); !result)
        return std::unexpected{result.error()};
    }
    if (!header_ || !nic_ || !card_.mac)
      return bad();
    return card_;
  }

private:
  std::unexpected<Error> bad() const {
    return std::unexpected{
        Error{Errc::Format, line_,
              "Invalid, duplicate or incomplete card snapshot field"}};
  }
  Status consume(std::string_view line) {
    if (++line_ > MaxLines)
      return bad();
    constexpr std::string_view whitespace = " \t\r\n";
    std::array<std::string_view, 7> tokens{};
    std::size_t count = 0;
    while (true) {
      const auto start = line.find_first_not_of(whitespace);
      if (start == line.npos)
        break;
      line.remove_prefix(start);
      if (count == 0 && line.front() == '#')
        return {};
      if (count == tokens.size())
        return bad();
      const auto end = line.find_first_of(whitespace);
      tokens[count++] = line.substr(0, end);
      if (end == line.npos)
        break;
      line.remove_prefix(end);
    }
    if (!count)
      return {};
    if (!header_) {
      if (count != 2 || tokens[0] != "P2CARD" || tokens[1] != "1")
        return bad();
      header_ = true;
      return {};
    }
    if (count == 2 && tokens[0] == "MAC") {
      if (card_.mac || tokens[1].size() != 17)
        return bad();
      std::array<uint8_t, 6> mac{};
      unsigned nonzero = 0;
      for (std::size_t i = 0; i < mac.size(); ++i) {
        const auto text = tokens[1].substr(i * 3, 2);
        unsigned value;
        const auto [end, error] =
            std::from_chars(text.data(), text.data() + 2, value, 16);
        if (error != std::errc{} || end != text.data() + 2 ||
            (i < 5 && tokens[1][i * 3 + 2] != ':'))
          return bad();
        mac[i] = static_cast<uint8_t>(value);
        nonzero |= value;
      }
      if (!nonzero || (mac[0] & 1))
        return bad();
      card_.mac = mac;
      return {};
    }
    if (count != 5 || (tokens[0] != "NIC" && tokens[0] != "SUP"))
      return bad();
    std::array<uint16_t, 4> numbers{};
    for (std::size_t i = 0; i < numbers.size(); ++i) {
      auto text = tokens[i + 1];
      if (text.starts_with('-') || text.starts_with('+'))
        return bad();
      const auto base =
          text.starts_with("0x") || text.starts_with("0X") ? 16 : 10;
      if (base == 16)
        text.remove_prefix(2);
      const auto [end, error] = std::from_chars(
          text.data(), text.data() + text.size(), numbers[i], base);
      if (error != std::errc{} || end != text.data() + text.size())
        return bad();
    }
    if (tokens[0] == "NIC") {
      if (nic_)
        return bad();
      nic_ = true;
      card_.nic = {numbers[0], numbers[1], numbers[2], numbers[3]};
    } else {
      const auto iface = numbers[0];
      if (!iface || iface > 4 || numbers[2] > numbers[3] ||
          card_.suppliers[iface - 1])
        return bad();
      card_.suppliers[iface - 1] =
          InterfaceRange{0, iface, numbers[1], numbers[2], numbers[3]};
    }
    return {};
  }
  CardSnapshot card_;
  unsigned long line_ = 0;
  bool header_ = false, nic_ = false;
};
} // namespace

Status Image::validate(Mode mode, const CardSnapshot &input,
                       std::span<const uint8_t> bytes, const Image *primary,
                       PlugPolicy policy) const {
  auto pda = PdaView::checked(bytes);
  if (!pda)
    return std::unexpected{pda.error()};
  auto card = pda->bind(input);
  if (!card)
    return std::unexpected{card.error()};
  if (primary) {
    if (primary->component().id != 0x15 || component().id != 0x1f)
      return fail(Errc::Incompatible, "Replacement pair must be PRI plus STA");
    if (auto result = check_image(*primary, mode, *card, *pda, policy); !result)
      return result;
    bool found = false;
    for (const auto &range : primary->ranges()) {
      if (!range.role && range.interface_id == 3) {
        if (found)
          return fail(Errc::Incompatible,
                      "Ambiguous replacement PRI supplier range");
        found = true;
        card->suppliers[2] = range;
      }
    }
    if (!found)
      return fail(Errc::Incompatible,
                  "Replacement primary has no PRI supplier range");
    for (const auto &a : primary->segments())
      for (const auto &b : segments())
        if (a.address < b.address + b.data.size() &&
            b.address < a.address + a.data.size())
          return fail(Errc::Incompatible,
                      "Primary/station payload ranges overlap");
  }
  return check_image(*this, mode, *card, *pda, policy);
}

Result<PreparedImage> PreparedImage::create(ImageSource target, Mode mode,
                                            const CardSnapshot &card,
                                            std::span<const uint8_t> bytes,
                                            std::optional<ImageSource> primary,
                                            PlugPolicy policy) {
  if (auto checked = target.image.validate(
          mode, card, bytes, primary ? &primary->image : nullptr, policy);
      !checked)
    return std::unexpected{checked.error()};
  if (mode == Mode::Flash && target.image.component().id == 0x15)
    return fail(Errc::Incompatible,
                "Primary-only flash preparation is prohibited");
  auto pda = PdaView::checked(bytes);
  if (!pda)
    return std::unexpected{pda.error()};
  PreparedImage prepared;
  prepared.entry_ = primary ? primary->image.entry() : target.image.entry();
  const auto copy_source = [&](ImageSource source) -> Status {
    if (source.basename.empty() || source.basename.size() > 255 ||
        source.basename.find_first_of("/\\:") != source.basename.npos ||
        std::any_of(source.basename.begin(), source.basename.end(),
                    [](unsigned char c) { return c < 32 || c > 126; }))
      return fail(Errc::Format, "A bounded ASCII source basename is required");
    for (const auto &segment : source.image.segments()) {
      const auto stamps =
          std::count_if(source.image.signatures().begin(),
                        source.image.signatures().end(), [&](const auto &s) {
                          return s.program && s.start == segment.address;
                        });
      if (stamps > 1)
        return fail(Errc::Incompatible, "Duplicate signature stamps");
      const std::size_t prefix = stamps ? 2 : 0;
      auto data = ByteBuffer::allocate(segment.data.size() + prefix);
      if (!data)
        return fail(Errc::Memory, "Out of memory preparing payload");
      auto payload = data.bytes().subspan(prefix);
      std::copy(segment.data.bytes().begin(), segment.data.bytes().end(),
                payload.begin());
      for (const auto &plug : source.image.plugs()) {
        if (!segment.contains(plug.address, plug.length))
          continue;
        auto destination =
            payload.subspan(plug.address - segment.address, plug.length);
        if (plug.pdr == UINT32_MAX) {
          std::fill(destination.begin(), destination.end(), 0);
          const auto count =
              policy == PlugPolicy::DosOptionalDefaults
                  ? std::min({source.basename.size(), destination.size(),
                              std::size_t{20}})
                  : std::min(source.basename.size(), destination.size() - 1);
          for (std::size_t i = 0; i < count; ++i) {
            char c = source.basename[i];
            if (policy == PlugPolicy::DosOptionalDefaults && c >= 'a' &&
                c <= 'z')
              c -= 'a' - 'A';
            destination[i] = static_cast<uint8_t>(c);
          }
        } else {
          const auto value = pda->find(static_cast<uint16_t>(plug.pdr));
          if ((!value || value->size() != destination.size()) &&
              optional_default(plug.pdr, policy)) {
            if (!prepared.fallbacks_.push_back(PlugFallback{
                    plug.pdr, plug.length,
                    value ? static_cast<uint32_t>(value->size()) : 0, !value}))
              return fail(Errc::Memory, "Out of memory recording DOS defaults");
            continue; // Keep the entire original image field, no partial copy.
          }
          if (!value || value->size() != destination.size())
            return fail(Errc::Pda, "PDA changed during preparation");
          std::copy(value->begin(), value->end(), destination.begin());
        }
      }
      if (prefix) {
        data.bytes()[0] = 0xde;
        data.bytes()[1] = 0xc0;
      }
      if (!prepared.segments_.push_back(
              Segment{segment.address - static_cast<uint32_t>(prefix),
                      std::move(data)}))
        return fail(Errc::Memory, "Out of memory retaining prepared segments");
    }
    return {};
  };
  if (primary) {
    if (auto copied = copy_source(*primary); !copied)
      return std::unexpected{copied.error()};
  }
  const auto primary_count = prepared.segments_.size();
  if (auto copied = copy_source(target); !copied)
    return std::unexpected{copied.error()};

  // Host AP's sole merge exception: a STA C0DE prefix can replace the final
  // two PRI bytes immediately before the original STA start. Never trim a
  // calibration plug or another signature to make an overlap fit.
  for (std::size_t i = 0; i < primary_count; ++i) {
    auto &a = prepared.segments_[i];
    for (std::size_t j = primary_count; j < prepared.segments_.size(); ++j) {
      const auto &b = prepared.segments_[j];
      const auto end = a.address + a.data.size();
      if (a.address >= b.address + b.data.size() || b.address >= end)
        continue;
      const auto stamp = std::find_if(
          target.image.signatures().begin(), target.image.signatures().end(),
          [&](const auto &s) {
            return s.program && s.start == b.address + 2 && s.start == end;
          });
      if (stamp == target.image.signatures().end() || a.data.size() < 2 ||
          b.address < a.address)
        return fail(Errc::Incompatible,
                    "Prepared primary/station ranges overlap");
      for (const auto &plug : primary->image.plugs())
        if (plug.address < end && plug.address + plug.length > end - 2)
          return fail(Errc::Incompatible,
                      "STA signature would overwrite a PRI PDA plug");
      for (const auto &signature : primary->image.signatures())
        if (signature.program && signature.start - 2 < end &&
            signature.start > end - 2)
          return fail(Errc::Incompatible,
                      "STA signature would overwrite a PRI signature");
      if (a.data.size() == 2) {
        a.data = {}; // PRI's standalone two-byte signature placeholder.
        continue;
      }
      auto shortened = ByteBuffer::allocate(a.data.size() - 2);
      if (!shortened)
        return fail(Errc::Memory, "Out of memory merging signature boundary");
      std::copy_n(a.data.bytes().begin(), shortened.size(),
                  shortened.bytes().begin());
      a.data = std::move(shortened);
    }
  }
  auto &segments = prepared.segments_;
  std::size_t retained = 0;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (!segments[i].data.size())
      continue;
    if (retained != i)
      segments[retained] = std::move(segments[i]);
    ++retained;
  }
  segments.truncate(retained);
  std::sort(segments.begin(), segments.end(),
            [](const auto &a, const auto &b) { return a.address < b.address; });
  for (std::size_t i = 1; i < segments.size(); ++i)
    if (segments[i - 1].address + segments[i - 1].data.size() >
        segments[i].address)
      return fail(Errc::Incompatible,
                  "Prepared payload/signature ranges overlap");
  // Coalesce adjacent bytes only. Gaps are never filled or programmed.
  std::size_t used = 0;
  for (std::size_t i = 0; i < segments.size();) {
    std::size_t j = i + 1, length = segments[i].data.size();
    while (j < segments.size() &&
           segments[i].address + length == segments[j].address)
      length += segments[j++].data.size();
    prepared.payload_bytes_ += length;
    if (j == i + 1) {
      if (used != i)
        segments[used] = std::move(segments[i]);
      ++used;
    } else {
      auto joined = ByteBuffer::allocate(length);
      if (!joined)
        return fail(Errc::Memory, "Out of memory coalescing prepared segments");
      auto destination = joined.bytes().begin();
      for (auto k = i; k < j; ++k)
        destination = std::copy(segments[k].data.bytes().begin(),
                                segments[k].data.bytes().end(), destination);
      const auto address = segments[i].address;
      segments[used++] = Segment{address, std::move(joined)};
    }
    i = j;
  }
  segments.truncate(used);
  return prepared;
}
Result<CardSnapshot> CardSnapshot::read(FILE &file) {
  return SnapshotParser{}.read(file);
}
} // namespace p2::firmware
