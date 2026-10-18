/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_BACKUP_H
#define P2_BACKUP_H
#include "prism2.h"
#include <stdio.h>
#define P2_NV_BYTES UINT32_C(0x800000)
struct p2_capture_result {
    uint32_t bytes[2], elapsed_us;
    int cleanup_error, backend_cleanup_error;
    char sha256[2][65];
    uint32_t mismatch_bytes, first_mismatch, comparison_read_bytes;
    uint16_t before[6], after[6];
};
int p2_capture_nv(struct p2_bus *, FILE *, FILE *, struct p2_capture_result *);
int p2_backup_command(const char *directory,const char *utc);
int p2_backup_valid_utc(const char *);
#endif
