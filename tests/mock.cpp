/* SPDX-License-Identifier: GPL-2.0-only */
#include "mock.h"
#include <stdlib.h>
#include <string.h>

static int touch(struct p2_mock *m) {
  ++m->operations;
  m->clock += m->access_delay_us;
  if (m->remove_at && m->operations >= m->remove_at)
    m->present = 0;
  if (m->cancel_at && m->operations >= m->cancel_at)
    m->cancel = 1;
  return m->present;
}
static uint16_t read16(void *ctx, uint16_t reg) {
  auto *m = static_cast<p2_mock *>(ctx);
  if (!touch(m))
    return 0xffff;
  if (reg == P2_CMD && m->stuck_command)
    return 0x8000;
  if (reg == P2_AUXDATA) {
    uint16_t v;
    const uint8_t *source = m->aux_space == 1 ? m->nv_memory : m->memory;
    if (!source)
      return 0xffff;
    if (m->aux_address >= 0x800000 - 1)
      return 0xffff;
    v = (uint16_t)(source[m->aux_address] |
                   ((uint16_t)source[m->aux_address + 1] << 8));
    m->aux_address += 2;
    if (m->aux_space == 1) {
      ++m->nv_reads;
      if (m->unstable_nv && (((m->nv_reads - 1) / 64) & 1))
        return v ^ 1;
      if ((m->corrupt_nv_after && m->nv_reads > m->corrupt_nv_after) ||
          m->nv_reads == m->corrupt_nv_once_at)
        return v ^ 1;
    }
    if (m->aux_space == 0) {
      ++m->ram_reads;
      if (m->ram_reads == m->corrupt_ram_once_at ||
          (m->unstable_ram && (((m->ram_reads - 1) / 64) & 1)))
        return v ^ 1;
    }
    if ((m->ccsr == 0x1f || m->ccsr == 0x0f) && (m->cor & 0x80) == 0 &&
        (m->corrupt_readback ||
         (m->sram_bits && m->ccsr != (m->sram_bits == 8 ? 0x1f : 0x0f))))
      return (uint16_t)(v ^ 0x0100);
    return v;
  }
  if (reg == P2_DATA0) {
    unsigned i = m->data_index++;
    uint16_t rid = m->regs[P2_SELECT0 / 2];
    if (i == 0)
      return m->bad_rid_length ? 4 : 5;
    if (i == 1)
      return rid;
    if (i == 2)
      return rid == P2_RID_NICID ? m->nicid : rid == P2_RID_PRIID ? 0x15 : 0x1f;
    if (i == 3 || i == 4)
      return 1;
    return rid == P2_RID_STAID ? 4 : 1;
  }
  return reg < 0x40 ? m->regs[reg / 2] : 0xffff;
}
static void write16(void *ctx, uint16_t reg, uint16_t value) {
  auto *m = static_cast<p2_mock *>(ctx);
  if (!touch(m) || reg >= 0x40)
    return;
  if (m->aux_disabled_by_host &&
      (reg == P2_PARAM0 || reg == P2_PARAM1 || reg == P2_PARAM2 ||
       reg == P2_AUXPAGE || reg == P2_AUXOFFSET))
    ++m->scratch_writes_after_disable;
  if (reg == P2_CONTROL)
    m->aux_disabled_by_host = (value & 0xc000) == 0x4000;
  if (reg == P2_CMD) {
    ++m->command_writes;
    if ((value & 0x3f) == 0x22)
      ++m->flash_commands;
    m->regs[P2_EVSTAT / 2] |= P2_EV_CMD;
    m->regs[P2_STATUS / 2] = m->command_failure ? 0x0100 : 0;
  } else if (reg == P2_EVACK) {
    m->regs[P2_EVSTAT / 2] &= (uint16_t)~value;
  } else if (reg == P2_CONTROL && !m->stuck_aux) {
    value = (value & 0xc000) == 0x8000 ? 0xc000 : 0;
  } else if (reg == P2_AUXDATA && m->aux_address < 0x800000 - 1) {
    if (!m->drop_ram_writes) {
      m->memory[m->aux_address] = static_cast<uint8_t>(value);
      m->memory[m->aux_address + 1] = static_cast<uint8_t>(value >> 8);
    }
    m->aux_address += 2;
    ++m->payload_writes;
  } else if (reg == P2_SELECT0 || reg == P2_OFFSET0)
    m->data_index = 0;
  m->regs[reg / 2] = value;
  if (reg == P2_AUXPAGE || reg == P2_AUXOFFSET)
    m->aux_address = ((uint32_t)m->regs[P2_AUXPAGE / 2] << 7) +
                     (m->regs[P2_AUXOFFSET / 2] & 127);
  if (reg == P2_AUXOFFSET)
    m->aux_space = (value >> 12) & 15;
}
static uint8_t read_cfg(void *ctx, uint8_t reg) {
  auto *m = static_cast<p2_mock *>(ctx);
  if (!touch(m))
    return 0xff;
  return reg == P2_CFG_COR ? m->cor : m->ccsr;
}
static void write_cfg(void *ctx, uint8_t reg, uint8_t value) {
  auto *m = static_cast<p2_mock *>(ctx);
  if (!touch(m))
    return;
  ++m->configuration_writes;
  if (reg == P2_CFG_COR)
    m->cor = value;
  else {
    m->ccsr = value;
    if (value == 0x1f || value == 0x0f)
      ++m->genesis_entries;
    if (value == 0x17 || value == 0x07) {
      ++m->normal_returns;
      if (m->stick_aux_on_normal)
        m->stuck_aux = 1;
    }
  }
}
static void delay_us(void *ctx, uint32_t us) {
  ((struct p2_mock *)ctx)->clock += us;
}
static uint32_t now_us(void *ctx) { return ((struct p2_mock *)ctx)->clock; }
static int present(void *ctx) { return ((struct p2_mock *)ctx)->present; }
static int cancelled(void *ctx) { return ((struct p2_mock *)ctx)->cancel; }
void p2_mock_fix_pda(uint8_t data[P2_PDA_SIZE]) {
  uint16_t crc = 0;
  size_t i;
  for (i = 0; i < 10; ++i) {
    unsigned b;
    crc ^= data[i];
    for (b = 0; b < 8; ++b)
      crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
  }
  data[10] = (uint8_t)crc;
  data[11] = (uint8_t)(crc >> 8);
}
int p2_mock_init(struct p2_mock *m, unsigned bits) {
  static const uint8_t pda[] = {2, 0, 8, 0, 0x1b, 0x80, 2, 0, 0, 0, 0, 0};
  memset(m, 0, sizeof(*m));
  m->memory = static_cast<uint8_t *>(calloc(1, 0x800000));
  if (!m->memory)
    return P2_E_IO;
  m->present = 1;
  m->cor = 0x41;
  m->ccsr = 8;
  m->sram_bits = bits;
  m->nicid = 0x801b;
  memcpy(m->memory + 0x7f0000, pda, sizeof(pda));
  p2_mock_fix_pda(m->memory + 0x7f0000);
  m->bus = p2_bus{m,        read16, write16, read_cfg, write_cfg,
                  delay_us, now_us, present, cancelled};
  return P2_OK;
}
void p2_mock_free(struct p2_mock *m) {
  free(m->memory);
  free(m->nv_memory);
  m->memory = nullptr;
  m->nv_memory = nullptr;
}
