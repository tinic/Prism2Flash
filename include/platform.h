/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_PLATFORM_H
#define P2_PLATFORM_H
#include "prism2.h"
struct p2_platform;
struct p2_card_info {
    uint16_t manufacturer, product;
    uint32_t config_base, io_base;
    uint8_t config_index;
};
int p2_platform_open(struct p2_platform **, struct p2_card_info *,
                     char *error, size_t error_size);
int p2_platform_configure(struct p2_platform *, struct p2_bus *,
                         char *error, size_t error_size);
void p2_platform_close(struct p2_platform *);
const char *p2_platform_description(void);
#endif
