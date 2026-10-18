/* SPDX-License-Identifier: GPL-2.0-only
 * Genesis/AUX sequence adapted from Linux v4.4 Host AP hostap_download.c
 * and hostap_cs.c, Copyright (c) 2001-2002 SSH Communications Security Corp
 * and Jouni Malinen; Copyright (c) 2002-2005 Jouni Malinen.
 * See docs/SOURCES.md for the pinned source files and provenance.
 */
#include "prism2.h"
#include <string.h>

static int alive(struct p2_bus *b)
{
    return b->present(b->ctx) ? P2_OK : P2_E_REMOVED;
}
static int interrupted(struct p2_bus *b)
{
    int rc = alive(b);
    if (rc) return rc;
    return b->cancelled && b->cancelled(b->ctx) ? P2_E_CANCELLED : P2_OK;
}
static int wait_mask(struct p2_bus *b, uint16_t reg, uint16_t mask,
                     uint16_t expected, uint32_t timeout)
{
    uint32_t start = b->now_us(b->ctx);
    for (;;) {
        int rc = interrupted(b);
        if (rc) return rc;
        if ((b->read16(b->ctx, reg) & mask) == expected) return P2_OK;
        if ((uint32_t)(b->now_us(b->ctx) - start) >= timeout)
            return P2_E_TIMEOUT;
        b->delay_us(b->ctx, 10);
    }
}
const char *p2_error_string(int rc)
{
    static const char *const names[] = {
        "OK", "invalid argument", "card removed", "hardware timeout",
        "I/O failure", "unsupported card", "RAM readback mismatch",
        "cancelled", "card command failed", "invalid PDA"
    };
    return rc >= 0 && (unsigned)rc < sizeof(names)/sizeof(names[0])
        ? names[rc] : "unknown error";
}
int p2_aux_enable(struct p2_bus *b, int enable)
{
    uint16_t control;
    int rc = wait_mask(b, P2_CMD, 0x8000, 0, 5000);
    if (rc) return rc;
    control = b->read16(b->ctx, P2_CONTROL);
    b->write16(b->ctx, P2_PARAM0, enable ? 0xfe01 : 0);
    b->write16(b->ctx, P2_PARAM1, enable ? 0xdc23 : 0);
    b->write16(b->ctx, P2_PARAM2, enable ? 0xba45 : 0);
    b->write16(b->ctx, P2_CONTROL,
               (uint16_t)((control & 0x3fff) | (enable ? 0x8000 : 0x4000)));
    b->delay_us(b->ctx, 5);
    return wait_mask(b, P2_CONTROL, 0xc000, enable ? 0xc000 : 0, 100000);
}
static int aux_bounds(uint32_t addr, size_t len)
{
    /* AUXPAGE is 16 bits; its offset has 7 bits. No silent truncation. */
    return (addr & 1) || (len & 1) || addr >= UINT32_C(0x800000)
        || len > UINT32_C(0x800000) - addr ? P2_E_ARGUMENT : P2_OK;
}
static int aux_transfer(struct p2_bus *b, uint32_t addr, uint8_t *read_buf,
                        const uint8_t *write_buf, size_t len)
{
    int rc = aux_bounds(addr, len);
    if (rc) return rc;
    while (len) {
        size_t i, n = 128 - (addr & 127);
        rc = interrupted(b);
        if (rc) return rc;
        if (n > len) n = len;
        b->write16(b->ctx, P2_AUXPAGE, (uint16_t)(addr >> 7));
        b->write16(b->ctx, P2_AUXOFFSET, (uint16_t)(addr & 127));
        b->delay_us(b->ctx, 5);
        for (i = 0; i < n; i += 2) {
            uint16_t v;
            if ((rc = interrupted(b)) != 0) return rc;
            if (write_buf) {
                v = (uint16_t)(write_buf[i] | ((uint16_t)write_buf[i+1] << 8));
                b->write16(b->ctx, P2_AUXDATA, v);
            } else {
                v = b->read16(b->ctx, P2_AUXDATA);
                read_buf[i] = (uint8_t)v;
                read_buf[i+1] = (uint8_t)(v >> 8);
            }
        }
        addr += (uint32_t)n;
        len -= n;
        if (write_buf) write_buf += n;
        else read_buf += n;
    }
    return alive(b);
}
int p2_aux_read(struct p2_bus *b, uint32_t a, uint8_t *p, size_t n)
{
    if (!p && n) return P2_E_ARGUMENT;
    return aux_transfer(b, a, p, NULL, n);
}
int p2_aux_write(struct p2_bus *b, uint32_t a, const uint8_t *p, size_t n)
{
    if (!p && n) return P2_E_ARGUMENT;
    return aux_transfer(b, a, NULL, p, n);
}
static int command(struct p2_bus *b, uint16_t cmd, uint16_t param)
{
    int rc;
    /* Deliberately no DOWNLOAD/PROGRAM (0x22) path in milestone one. */
    if (cmd != 0 && cmd != 0x21) return P2_E_ARGUMENT;
    rc = wait_mask(b, P2_CMD, 0x8000, 0, 5000);
    if (rc) return rc;
    b->write16(b->ctx, P2_EVACK, P2_EV_CMD);
    b->write16(b->ctx, P2_PARAM0, param);
    b->write16(b->ctx, P2_CMD, cmd);
    rc = wait_mask(b, P2_EVSTAT, P2_EV_CMD, P2_EV_CMD, 500000);
    if (rc) return rc;
    rc = (b->read16(b->ctx, P2_STATUS) & 0x7f00) ? P2_E_COMMAND : P2_OK;
    b->write16(b->ctx, P2_EVACK, P2_EV_CMD);
    return rc;
}
int p2_initialize(struct p2_bus *b)
{
    int rc = interrupted(b);
    if (rc) return rc;
    b->write16(b->ctx, P2_INTEN, 0);
    b->write16(b->ctx, P2_EVACK, 0xffff);
    return command(b, 0, 0);
}
int p2_identity(struct p2_bus *b, uint16_t rid, struct p2_identity *id)
{
    uint16_t words, type;
    int rc;
    if (!id || (rid != P2_RID_NICID && rid != P2_RID_PRIID && rid != P2_RID_STAID))
        return P2_E_ARGUMENT;
    memset(id, 0, sizeof(*id));
    rc = command(b, 0x21, rid);
    if (rc) return rc;
    b->write16(b->ctx, P2_SELECT0, rid);
    b->write16(b->ctx, P2_OFFSET0, 0);
    rc = wait_mask(b, P2_OFFSET0, 0x8000, 0, 50000);
    if (rc) return rc;
    if (b->read16(b->ctx, P2_OFFSET0) & 0x4000) return P2_E_IO;
    words = b->read16(b->ctx, P2_DATA0);
    type = b->read16(b->ctx, P2_DATA0);
    if (words != 5 || type != rid) return P2_E_IO;
    if ((rc = interrupted(b)) != 0) return rc;
    id->id = b->read16(b->ctx, P2_DATA0);
    id->variant = b->read16(b->ctx, P2_DATA0);
    id->major = b->read16(b->ctx, P2_DATA0);
    id->minor = b->read16(b->ctx, P2_DATA0);
    return alive(b);
}
int p2_supported_nic(uint16_t id)
{
    /* Ordinary HFA3842 platforms from the Intersil release-ID map. Early
     * HFA3841, SSF and ambiguous 0x800c await dedicated profiles. The native
     * backend independently restricts the bus to known PC Card CIS IDs. */
    return id == 0x800b || id == 0x800d || id == 0x8012 || id == 0x8016
        || id == 0x801a || id == 0x8017 || id == 0x801b || id == 0x8018
        || id == 0x801c;
}
int p2_read_pda(struct p2_bus *b, uint8_t data[P2_PDA_SIZE], uint32_t *location)
{
    static const uint32_t addresses[] = { 0x7f0000, 0x3f0000, 0x390000, 0x7f0002 };
    size_t i;
    int rc = p2_aux_enable(b, 1), cleanup;
    if (rc) return rc;
    for (i = 0; i < sizeof(addresses)/sizeof(addresses[0]); ++i) {
        rc = p2_aux_read(b, addresses[i], data, P2_PDA_SIZE);
        if (rc) break;
        rc = p2_validate_pda(data);
        if (!rc) { if (location) *location = addresses[i]; break; }
    }
    {
        /* Cancellation must not prevent closing an AUX session we opened. */
        int (*cancel_fn)(void *) = b->cancelled;
        b->cancelled = NULL;
        cleanup = p2_aux_enable(b, 0);
        b->cancelled = cancel_fn;
    }
    return rc ? rc : cleanup;
}
static int genesis_reset(struct p2_bus *b, uint8_t cor, uint8_t hcr)
{
    int rc = alive(b);
    if (rc) return rc;
    b->write_cfg(b->ctx, P2_CFG_COR, (uint8_t)(cor | 0x80));
    b->delay_us(b->ctx, 10000);
    if ((rc = alive(b)) != 0) return rc;
    b->write_cfg(b->ctx, P2_CFG_CCSR, hcr);
    b->delay_us(b->ctx, 10000);
    if ((rc = alive(b)) != 0) return rc;
    b->write_cfg(b->ctx, P2_CFG_COR, (uint8_t)(cor & 0x7f));
    b->delay_us(b->ctx, 10000);
    return alive(b);
}
int p2_genesis_probe(struct p2_bus *b, unsigned bits, struct p2_probe_result *out)
{
    static const uint8_t pattern[] = { 0x00, 0xe1, 0xa1, 0xff };
    uint8_t discard[4];
    int rc, restore;
    int was_cancelled;
    int (*cancel_fn)(void *);
    if (!out || (bits != 8 && bits != 16)) return P2_E_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->sram_bits = bits;
    if ((rc = interrupted(b)) != 0) return rc;
    out->cor_before = b->read_cfg(b->ctx, P2_CFG_COR);
    out->ccsr_before = b->read_cfg(b->ctx, P2_CFG_CCSR);
    rc = p2_aux_enable(b, 1);
    if (rc) return rc;
    /* Host AP resets COR, writes its bootstrap sentinel, then latches HCR. */
    b->write_cfg(b->ctx, P2_CFG_COR, (uint8_t)(out->cor_before | 0x80));
    b->delay_us(b->ctx, 2000);
    if ((rc = interrupted(b)) == 0) {
        b->write_cfg(b->ctx, P2_CFG_COR, (uint8_t)(out->cor_before & 0x7f));
        b->delay_us(b->ctx, 2000);
        rc = p2_aux_write(b, P2_GENESIS_ADDRESS, pattern, sizeof(pattern));
    }
    if (!rc) rc = genesis_reset(b, out->cor_before, bits == 8 ? 0x1f : 0x0f);
    if (!rc) rc = p2_aux_read(b, P2_GENESIS_ADDRESS, discard, sizeof(discard));
    if (!rc) rc = p2_aux_write(b, P2_GENESIS_ADDRESS, pattern, sizeof(pattern));
    if (!rc) rc = p2_aux_read(b, P2_GENESIS_ADDRESS, out->readback, sizeof(out->readback));
    if (!rc && memcmp(pattern, out->readback, sizeof(pattern))) rc = P2_E_VERIFY;
    /* Ctrl-C may stop the test, but must not prevent returning to normal mode.
     * Removal still forbids further I/O. Do not reset/reclaim another card. */
    cancel_fn = b->cancelled;
    was_cancelled = cancel_fn && cancel_fn(b->ctx);
    b->cancelled = NULL;
    restore = genesis_reset(b, out->cor_before, bits == 8 ? 0x17 : 0x07);
    if (!restore) restore = p2_aux_enable(b, 0);
    out->cleanup_error = restore;
    b->cancelled = cancel_fn;
    if (!rc && was_cancelled) rc = P2_E_CANCELLED;
    return rc ? rc : restore;
}
