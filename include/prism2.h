/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PRISM2_H
#define PRISM2_H
#include <stddef.h>
#include <stdint.h>

enum p2_error {
    P2_OK = 0, P2_E_ARGUMENT, P2_E_REMOVED, P2_E_TIMEOUT,
    P2_E_IO, P2_E_UNSUPPORTED, P2_E_VERIFY, P2_E_CANCELLED,
    P2_E_COMMAND, P2_E_PDA
};
enum p2_register {
    P2_CMD = 0x00, P2_PARAM0 = 0x02, P2_PARAM1 = 0x04,
    P2_PARAM2 = 0x06, P2_STATUS = 0x08, P2_CONTROL = 0x14,
    P2_SELECT0 = 0x18, P2_OFFSET0 = 0x1c, P2_EVSTAT = 0x30,
    P2_INTEN = 0x32, P2_EVACK = 0x34, P2_DATA0 = 0x36,
    P2_AUXPAGE = 0x3a, P2_AUXOFFSET = 0x3c, P2_AUXDATA = 0x3e
};
enum { P2_CFG_COR = 0, P2_CFG_CCSR = 2, P2_EV_CMD = 0x10 };
enum { P2_RID_NICID = 0xfd0b, P2_RID_PRIID = 0xfd02,
       P2_RID_STAID = 0xfd20 };
#define P2_PDA_SIZE 1024u
#define P2_GENESIS_ADDRESS UINT32_C(0x7e0038)

/* Register values are host integers. AUX payloads are ALWAYS little-endian
 * byte streams. The backend performs register byte swapping exactly once. */
struct p2_bus {
    void *ctx;
    uint16_t (*read16)(void *, uint16_t);
    void (*write16)(void *, uint16_t, uint16_t);
    uint8_t (*read_cfg)(void *, uint8_t);
    void (*write_cfg)(void *, uint8_t, uint8_t);
    void (*delay_us)(void *, uint32_t);
    uint32_t (*now_us)(void *);
    int (*present)(void *);
    int (*cancelled)(void *);
};
struct p2_identity { uint16_t id, variant, major, minor; };
struct p2_probe_result {
    unsigned sram_bits;
    uint8_t cor_before, ccsr_before;
    uint8_t readback[4];
    int cleanup_error;
};
const char *p2_error_string(int);
int p2_aux_enable(struct p2_bus *, int enable);
int p2_aux_read(struct p2_bus *, uint32_t, uint8_t *, size_t);
/* Nonvolatile address space, AUXOFFSET selector 1; never writes AUXDATA. */
int p2_aux_read_nv(struct p2_bus *, uint32_t, uint8_t *, size_t);
int p2_aux_write(struct p2_bus *, uint32_t, const uint8_t *, size_t);
int p2_initialize(struct p2_bus *);
int p2_identity(struct p2_bus *, uint16_t rid, struct p2_identity *);
int p2_supported_nic(uint16_t);
int p2_read_pda(struct p2_bus *, uint8_t[P2_PDA_SIZE], uint32_t *);
int p2_validate_pda(const uint8_t[P2_PDA_SIZE]);
int p2_genesis_probe(struct p2_bus *, unsigned, struct p2_probe_result *);
#endif
