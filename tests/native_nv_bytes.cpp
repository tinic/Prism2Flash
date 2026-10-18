/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "file.h"
#include "p2_stdio.h"
#include "platform.h"
#include "preflight.h"
#include "sha256.h"
#include "storage.h"

// Native read-only transport diagnostic. No INIT, COR/HCR reset, JEDEC or
// payload write. Save word/byte PDA views from one exclusive insertion.
int main(int argc, char **argv) {
  if (argc != 3)
    return 20;
  auto session = p2::preflight::NativeSession::create(argv[1], argv[2], -300);
  if (!session)
    return 20;
  auto words = p2::ByteBuffer::allocate(P2_PDA_SIZE);
  auto bytes = p2::ByteBuffer::allocate(P2_PDA_SIZE);
  auto word_file = p2::File::open("pda-word.bin", "w+b");
  auto byte_file = p2::File::open("pda-byte.bin", "w+b");
  if (!words || !bytes || !word_file || !byte_file)
    return 20;
  auto slot = p2::Slot::open();
  if (!slot) {
    p2_fprintf(stderr, "Slot: %s\n", slot.error().message.data());
    return 20;
  }
  auto bus = slot->configure(true);
  if (!bus) {
    p2_fprintf(stderr, "Configure: %s\n", bus.error().message.data());
    return 20;
  }
  p2_printf("COR=%02x CCSR=%02x CMD=%04x CONTROL=%04x\n",
            bus->read_cfg(bus->ctx, P2_CFG_COR),
            bus->read_cfg(bus->ctx, P2_CFG_CCSR), bus->read16(bus->ctx, P2_CMD),
            bus->read16(bus->ctx, P2_CONTROL));
  p2::Card card{*bus};
  if (!card.read_nv(0x7f0000, words.bytes()))
    return 20;
  for (uint32_t i = 0; i < P2_PDA_SIZE; ++i) {
    auto value = card.read_nv_byte(0x7f0000 + i);
    if (!value)
      return 20;
    bytes.bytes()[i] = *value;
  }
  // Required by the established Amiga read-path cleanup: a live NV selector
  // can obscure attribute CIS reads. Clear it while AUX remains enabled.
  bus->write16(bus->ctx, P2_AUXOFFSET, 0);
  p2::Sha256 word_hash, byte_hash;
  word_hash.update(words.bytes());
  byte_hash.update(bytes.bytes());
  if (fwrite(words.bytes().data(), 1, words.size(), word_file.get()) !=
          words.size() ||
      fwrite(bytes.bytes().data(), 1, bytes.size(), byte_file.get()) !=
          bytes.size() ||
      !word_file.close() || !byte_file.close() ||
      p2::backup::verify_file("pda-word.bin", word_hash.hex().data(),
                              words.size()) ||
      p2::backup::verify_file("pda-byte.bin", byte_hash.hex().data(),
                              bytes.size()))
    return 20;
  p2_printf("Word PDA validation=%d SHA256=%s\n",
            p2_validate_pda(words.bytes().data()), word_hash.hex().data());
  p2_printf("Byte PDA validation=%d SHA256=%s\n",
            p2_validate_pda(bytes.bytes().data()), byte_hash.hex().data());
  return slot->finish_backup() ? 20 : 0;
}
