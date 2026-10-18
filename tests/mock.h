/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_MOCK_H
#define P2_MOCK_H
#include "prism2.h"
struct p2_mock {
    uint8_t *memory;
    uint8_t *nv_memory;
    uint16_t regs[32];
    uint8_t cor, ccsr;
    unsigned sram_bits;
    uint16_t nicid;
    uint32_t clock, aux_address;
    unsigned data_index, operations, remove_at, cancel_at;
    unsigned flash_commands, command_writes, genesis_entries, normal_returns;
    unsigned configuration_writes, payload_writes;
    unsigned aux_space, nv_reads, corrupt_nv_after;
    int present, cancel, stuck_command, stuck_aux, corrupt_readback;
    int bad_rid_length, command_failure;
    struct p2_bus bus;
};
int p2_mock_init(struct p2_mock *, unsigned);
void p2_mock_free(struct p2_mock *);
void p2_mock_fix_pda(uint8_t[P2_PDA_SIZE]);
#endif
