/* SPDX-License-Identifier: GPL-2.0-only */
#include "prism2.h"

int p2_validate_pda(const uint8_t data[P2_PDA_SIZE]) {
  size_t pos = 0;
  uint16_t crc = 0;
  while (pos + 4 <= P2_PDA_SIZE) {
    size_t words = data[pos] | ((size_t)data[pos + 1] << 8);
    unsigned type = data[pos + 2] | ((unsigned)data[pos + 3] << 8);
    size_t end, i;
    if (!words || words > (P2_PDA_SIZE - pos) / 2 - 1)
      return P2_E_PDA;
    if (type == 0 && words != 2)
      return P2_E_PDA;
    end = pos + (words + 1) * 2;
    if (type == 0 && words == 2) {
      for (i = 0; i < end; ++i) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
          crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
      }
      return crc == 0 ? P2_OK : P2_E_PDA;
    }
    pos = end;
  }
  return P2_E_PDA;
}
