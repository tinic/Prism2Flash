/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_SHA256_H
#define P2_SHA256_H
#include <stddef.h>
#include <stdint.h>
struct p2_sha256 {
  uint32_t h[8];
  uint64_t bytes;
  size_t used;
  uint8_t block[64];
};
void p2_sha256_init(struct p2_sha256 *);
void p2_sha256_update(struct p2_sha256 *, const void *, size_t);
void p2_sha256_hex(struct p2_sha256 *, char[65]);
#endif
