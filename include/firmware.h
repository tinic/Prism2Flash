/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_FIRMWARE_H
#define P2_FIRMWARE_H
#include "prism2.h"
#include <stdio.h>

#define P2_FW_MAX_ITEMS 256u
#define P2_FW_MAX_CRCS 64u
#define P2_FW_MAX_SEGMENTS 8192u
#define P2_FW_MAX_BYTES (1024ul * 1024ul)
#define P2_FW_MAX_INPUT (8ul * 1024ul * 1024ul)
#define P2_FW_MAX_LINES 65536ul

enum p2_fw_mode { P2_FW_RAM, P2_FW_FLASH };
enum p2_fw_result {
  P2_FW_OK,
  P2_FW_FORMAT,
  P2_FW_LIMIT,
  P2_FW_MEMORY,
  P2_FW_IO,
  P2_FW_INCOMPATIBLE,
  P2_FW_PDA
};
struct p2_fw_error {
  int code;
  unsigned long line;
  char message[160];
};
struct p2_fw_segment {
  uint32_t address;
  size_t length;
  uint8_t *data;
};
struct p2_fw_range {
  uint16_t role, interface_id, variant, bottom, top;
};
struct p2_fw_plug {
  uint32_t pdr, address, length;
};
/* Host AP calls these CRC records but stamps DE C0; it calculates no image CRC.
 */
struct p2_fw_crc {
  uint32_t start, length, program;
};
struct p2_fw_image {
  struct p2_identity component;
  uint32_t entry;
  unsigned long records;
  size_t payload_bytes, segment_count;
  struct p2_fw_segment *segments;
  size_t platform_count, range_count, plug_count, crc_count;
  struct p2_identity platforms[P2_FW_MAX_ITEMS];
  struct p2_fw_range ranges[P2_FW_MAX_ITEMS];
  struct p2_fw_plug plugs[P2_FW_MAX_ITEMS];
  struct p2_fw_crc crcs[P2_FW_MAX_CRCS];
  char name[128];
  uint16_t build_sequence;
  unsigned has_sequence;
};
struct p2_fw_card {
  struct p2_identity nic;
  uint8_t mac[6];
  unsigned has_mac;
  unsigned supplier_mask;
  struct p2_fw_range suppliers[4];
};
/* Parsing never calls a card backend. On failure *out is NULL. */
int p2_fw_parse(const uint8_t *, size_t, struct p2_fw_image **,
                struct p2_fw_error *);
int p2_fw_read(FILE *, struct p2_fw_image **, struct p2_fw_error *);
void p2_fw_free(struct p2_fw_image *);
int p2_fw_card_read(FILE *, struct p2_fw_card *, struct p2_fw_error *);
/* Checks metadata and PDA requirements only; does not patch or emit firmware.
 * Optional primary supplies its replacement PRI interface for a station image.
 * Hardware address-map approval and actual prepare/merge remain separate gates.
 */
int p2_fw_validate(const struct p2_fw_image *, enum p2_fw_mode,
                   const struct p2_fw_card *, const uint8_t *, size_t,
                   const struct p2_fw_image *primary, struct p2_fw_error *);
#endif
