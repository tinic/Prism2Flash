/* SPDX-License-Identifier: GPL-2.0-only
 * PCMCIA setup follows Neil Cafferkey's prism2.device pccard.c (GPL-2.0-or-later).
 * card.resource call stubs follow AmiNetXDuo netdev_pcmcia.c (MIT), copyright
 * AmiNetXDuo contributors. See third_party/NOTICES and docs/SOURCES.md.
 */
#include "platform.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <resources/card.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <libraries/pccard.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include "p2_stdio.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct UtilityBase *UtilityBase;
static struct Library *card_resource;
struct p2_platform {
    struct CardHandle handle;
    struct Interrupt removed;
    struct CardMemoryMap *map;
    struct Library *pccard;
    struct MsgPort *timer_port;
    struct timerequest *timer;
    struct p2_card_info info;
    volatile UBYTE present;
    volatile UBYTE cancelled;
    int owned, configured, timer_open;
    int backup_only, cor_changed;
    uint8_t original_cor;
    volatile UBYTE *config;
    volatile UBYTE *io;
};
static struct p2_platform *active_platform;
static struct CardHandle *own_card(struct CardHandle *h)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardHandle *a1 __asm("a1") = h;
    register struct CardHandle *ret __asm("d0");
    __asm volatile("jsr a6@(-6)" : "=r"(ret), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
    return ret;
}
static void release_card(struct CardHandle *h)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardHandle *a1 __asm("a1") = h;
    register ULONG d0 __asm("d0") = CARDF_REMOVEHANDLE;
    __asm volatile("jsr a6@(-12)" : "+r"(d0), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
}
static struct CardMemoryMap *card_map(void)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardMemoryMap *ret __asm("d0");
    __asm volatile("jsr a6@(-18)" : "=r"(ret) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return ret;
}
static int copy_tuple(struct CardHandle *h, UBYTE *buf, ULONG type)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardHandle *a1 __asm("a1") = h;
    register UBYTE *a0 __asm("a0") = buf;
    register ULONG d1 __asm("d1") = type;
    register ULONG d0 __asm("d0") = 255;
    __asm volatile("jsr a6@(-72)" : "+r"(d0), "+r"(a1), "+r"(a0), "+r"(d1) : "r"(a6) : "cc", "memory");
    return d0 != 0;
}
static void reset_card(struct CardHandle *h)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardHandle *a1 __asm("a1") = h;
    register ULONG ret __asm("d0");
    __asm volatile("jsr a6@(-66)" : "=r"(ret), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
}
static void misc_control(struct CardHandle *h, UBYTE bits)
{
    register struct Library *a6 __asm("a6") = card_resource;
    register struct CardHandle *a1 __asm("a1") = h;
    register ULONG d1 __asm("d1") = bits;
    register ULONG ret __asm("d0");
    __asm volatile("jsr a6@(-48)" : "=r"(ret), "+r"(a1), "+r"(d1)
                   : "r"(a6) : "a0", "cc", "memory");
}
static const struct TagItem *tuple_info(struct Library *lib, UBYTE *buf)
{
    register struct Library *a6 __asm("a6") = lib;
    register UBYTE *a0 __asm("a0") = buf;
    register const struct TagItem *ret __asm("d0");
    __asm volatile("jsr a6@(-30)" : "=r"(ret), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
    return ret;
}
static void free_tuple(struct Library *lib, const struct TagItem *tags)
{
    register struct Library *a6 __asm("a6") = lib;
    register const struct TagItem *a0 __asm("a0") = tags;
    __asm volatile("jsr a6@(-36)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
}
static VOID removed(void)
{
    /* One slot/session. No C argument ABI at an interrupt entry; A1 holds
     * is_Data but this handler deliberately uses only static state. */
    if (active_platform) active_platform->present = 0;
}
static int present(void *ctx) { return ((struct p2_platform *)ctx)->present != 0; }
static int cancelled(void *ctx)
{
    struct p2_platform *p = ctx;
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) p->cancelled = 1;
    return p->cancelled;
}
static uint32_t now_us(void *ctx)
{
    struct p2_platform *p = ctx;
    struct timeval tv;
    register struct Device *a6 __asm("a6") = p->timer->tr_node.io_Device;
    register struct timeval *a0 __asm("a0") = &tv;
    __asm volatile("jsr a6@(-66)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
    return (uint32_t)tv.tv_secs * UINT32_C(1000000) + (uint32_t)tv.tv_micro;
}
static void delay_us(void *ctx, uint32_t us)
{
    struct p2_platform *p = ctx;
    if (us >= 1000) {
        p->timer->tr_node.io_Command = TR_ADDREQUEST;
        p->timer->tr_time.tv_secs = us / 1000000;
        p->timer->tr_time.tv_micro = us % 1000000;
        DoIO((struct IORequest *)p->timer);
    } else {
        uint32_t start = now_us(ctx);
        while ((uint32_t)(now_us(ctx) - start) < us) { }
    }
}
static uint16_t read16(void *ctx, uint16_t reg)
{
    struct p2_platform *p = ctx;
    uint16_t v;
    if (!p->present) return 0xffff;
    v = *(volatile UWORD *)(p->io + reg);
    return (uint16_t)((v << 8) | (v >> 8));
}
static void write16(void *ctx, uint16_t reg, uint16_t value)
{
    struct p2_platform *p = ctx;
    if (p->present)
        *(volatile UWORD *)(p->io + reg) = (UWORD)((value << 8) | (value >> 8));
}
static uint8_t read_cfg(void *ctx, uint8_t reg)
{
    struct p2_platform *p = ctx;
    return p->present ? p->config[reg] : 0xff;
}
static void write_cfg(void *ctx, uint8_t reg, uint8_t value)
{
    struct p2_platform *p = ctx;
    if (p->present) p->config[reg] = value;
}
static int supported_cis(uint16_t man, uint16_t card)
{
    /* Narrow, unambiguous PC Card entries from Host AP; no Hermes/Symbol
     * shared IDs, multifunction CF cards, or arbitrary force override. */
    return (man == 0x0274 && (card == 0x1612 || card == 0x1613))
        || (man == 0x0250 && card == 0x0002)
        || (man == 0x028a && card == 0x0002)
        || (man == 0x02aa && card == 0x0002)
        || (man == 0x02d2 && card == 0x0001);
}
int p2_platform_open(struct p2_platform **out, struct p2_card_info *info,
                     char *error, size_t size)
{
    struct p2_platform *p = calloc(1, sizeof(*p));
    struct CardHandle *owner;
    UBYTE tuple[263];
    if (!p) { p2_snprintf(error, size, "Out of memory"); return P2_E_IO; }
    *out = NULL;
    card_resource = OpenResource((CONST_STRPTR)CARDRESNAME);
    if (!card_resource) { p2_snprintf(error, size, "No card.resource; use an A600/A1200"); goto fail; }
    p->handle.cah_CardNode.ln_Name = (char *)"Prism2Genesis";
    p->handle.cah_CardFlags = CARDF_IFAVAILABLE;
    p->removed.is_Data = p;
    p->removed.is_Code = removed;
    p->handle.cah_CardRemoved = &p->removed;
    p->present = 1; /* Set before OwnCard; preserve removal during claim. */
    active_platform = p;
    owner = own_card(&p->handle);
    if (owner) {
        p2_snprintf(error, size, "Cannot own PCMCIA card: absent or held by a driver");
        goto fail;
    }
    p->owned = 1;
    p->map = card_map();
    if (!p->map || !p->map->cmm_AttributeMemory || !p->map->cmm_IOMemory) {
        p2_snprintf(error, size, "PCMCIA attribute/I/O mapping is unavailable"); goto fail;
    }
    memset(tuple, 0, sizeof(tuple));
    if (!copy_tuple(&p->handle, tuple, PCCARD_TPL_MANFID) || tuple[1] < 4) {
        p2_snprintf(error, size, "Cannot read MANFID tuple; damaged CIS recovery is a later milestone"); goto fail;
    }
    p->info.manufacturer = (uint16_t)(tuple[2] | ((uint16_t)tuple[3] << 8));
    p->info.product = (uint16_t)(tuple[4] | ((uint16_t)tuple[5] << 8));
    *info = p->info;
    *out = p;
    return P2_OK;
fail:
    p2_platform_close(p);
    return P2_E_IO;
}
static int configure(struct p2_platform *p, struct p2_bus *bus,
                     char *error, size_t size, int backup_only)
{
    UBYTE tuple[263];
    const struct TagItem *tags;
    ULONG *bases, *lengths, count, i;
    int found = 0;
    if (backup_only) {
        static const char strings[] = "NETGEAR MA401RA Wireless PC\0Card\0ISL37300P\0Eval-RevA\0";
        memset(tuple, 0, sizeof(tuple));
        if (p->info.manufacturer != 0x000b || p->info.product != 0x7300
            || !copy_tuple(&p->handle, tuple, PCCARD_TPL_VERS1)
            || tuple[1] < 2 + sizeof(strings) - 1
            || memcmp(tuple + 4, strings, sizeof(strings) - 1)) {
            p2_snprintf(error, size, "Backup requires the exact MA401RA / ISL37300P CIS identity");
            return P2_E_UNSUPPORTED;
        }
        p->backup_only = 1;
    } else if (!supported_cis(p->info.manufacturer, p->info.product)) {
        p2_snprintf(error, size, "CIS ID is outside the initial Prism2 PC Card allowlist");
        return P2_E_UNSUPPORTED;
    }
    UtilityBase = (struct UtilityBase *)OpenLibrary((CONST_STRPTR)"utility.library", 37);
    p->pccard = OpenLibrary((CONST_STRPTR)PCCARDNAME, 2);
    if (!UtilityBase || !p->pccard || (p->pccard->lib_Version == 2 && p->pccard->lib_Revision < 2)) {
        p2_snprintf(error, size, "Need utility.library and pccard.library 2.2 or newer"); return P2_E_IO;
    }
    memset(tuple, 0, sizeof(tuple));
    if (!p->present || !copy_tuple(&p->handle, tuple, PCCARD_TPL_CONFIG)) goto bad_cis;
    tags = tuple_info(p->pccard, tuple);
    if (!tags) goto bad_cis;
    p->info.config_base = GetTagData(PCCARD_RegisterBase, 0xffffffff, tags);
    free_tuple(p->pccard, tags);
    memset(tuple, 0, sizeof(tuple));
    if (!p->present || !copy_tuple(&p->handle, tuple, PCCARD_TPL_CFTABLEENTRY)) goto bad_cis;
    tags = tuple_info(p->pccard, tuple);
    if (!tags) goto bad_cis;
    p->info.config_index = (uint8_t)GetTagData(PCCARD_ModeNo, 0, tags);
    bases = (ULONG *)GetTagData(PCCARD_IOWinBases, 0, tags);
    lengths = (ULONG *)GetTagData(PCCARD_IOWinLengths, 0, tags);
    count = GetTagData(PCCARD_IOWinCount, 0, tags);
    if (bases && lengths && count <= 16) {
        for (i = 0; i < count; ++i) {
            if (lengths[i] == 0x40 && !(bases[i] & 1)) {
                p->info.io_base = bases[i]; found = 1; break;
            }
        }
    }
    free_tuple(p->pccard, tags);
    /* Conservative V37 sizes; V39 maps carry explicit lengths. COR/CCSR
     * offsets from pccard.library already use mapped attribute units. */
    if (!found || p->info.config_index == 0 || p->info.config_index > 0x3f
        || p->info.config_base > 0x20000 - 3 || p->info.io_base > 0x10000 - 0x40)
        goto bad_cis;
    if (card_resource->lib_Version >= 39 &&
        (p->map->cmm_AttributeMemSize < 3 || p->map->cmm_IOMemSize < 0x40 ||
         p->info.config_base > p->map->cmm_AttributeMemSize - 3 ||
         p->info.io_base > p->map->cmm_IOMemSize - 0x40)) goto bad_cis;
    p->timer_port = CreateMsgPort();
    if (p->timer_port) p->timer = (struct timerequest *)CreateIORequest(p->timer_port, sizeof(*p->timer));
    if (!p->timer || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)p->timer, 0)) {
        p2_snprintf(error, size, "Cannot open timer.device"); return P2_E_IO;
    }
    p->timer_open = 1;
    p->config = p->map->cmm_AttributeMemory + p->info.config_base;
    p->io = p->map->cmm_IOMemory + p->info.io_base;
    if (!p->present) { p2_snprintf(error, size, "Card removed during configuration"); return P2_E_REMOVED; }
    p2_printf("Mapping: attribute offset=0x%08lx I/O offset=0x%08lx index=%u\n",
           (unsigned long)p->info.config_base, (unsigned long)p->info.io_base,
           (unsigned)p->info.config_index);
    if (backup_only) {
        p->original_cor = read_cfg(p, P2_CFG_COR);
        if ((p->original_cor & 0x80) || ((p->original_cor & 0x3f) != 0
            && (p->original_cor & 0x3f) != p->info.config_index)) {
            p2_snprintf(error, size, "Refusing reset or replacement of a different active I/O configuration");
            return P2_E_UNSUPPORTED;
        }
        if ((p->original_cor & 0x3f) == 0) {
            p->cor_changed = 1;
            write_cfg(p, P2_CFG_COR, (uint8_t)(p->original_cor | p->info.config_index));
        }
    } else {
        /* Same slot-pin setup as prism2.device's PC Card backend. */
        misc_control(&p->handle, CARD_ENABLEF_DIGAUDIO | CARD_DISABLEF_WP);
        write_cfg(p, P2_CFG_COR, p->info.config_index);
        write_cfg(p, P2_CFG_CCSR, (uint8_t)(read_cfg(p, P2_CFG_CCSR) | PCCARD_REG_CCSRF_AUDIOENABLE));
    }
    p->configured = 1;
    delay_us(p, 50000);
    *bus = (struct p2_bus){p, read16, write16, read_cfg, write_cfg,
                           delay_us, now_us, present, cancelled};
    return P2_OK;
bad_cis:
    p2_snprintf(error, size, "Unsupported CIS configuration; first CFTABLE entry must contain a 64-byte I/O window");
    return P2_E_UNSUPPORTED;
}
int p2_platform_configure(struct p2_platform *p, struct p2_bus *bus,
                         char *error, size_t size)
{
    return configure(p, bus, error, size, 0);
}
int p2_platform_configure_backup(struct p2_platform *p, struct p2_bus *bus,
                                char *error, size_t size)
{
    return configure(p, bus, error, size, 1);
}
int p2_platform_backup_directory(const char *path)
{
    BPTR lock;
    /* Use a persistent volume explicitly; never stage a backup in RAM/T. */
    if (!strchr(path, ':') || !strncasecmp(path, "RAM:", 4) || !strncasecmp(path, "T:", 2))
        return P2_E_ARGUMENT;
    lock = CreateDir((CONST_STRPTR)path);
    if (!lock) return P2_E_IO; /* Existing directory is never reused. */
    UnLock(lock);
    return P2_OK;
}
int p2_platform_finish_backup(struct p2_platform *p)
{
    if(!p || !p->backup_only)return P2_E_ARGUMENT;
    if(!p->configured)return P2_OK;
    if(!p->present)return P2_E_REMOVED;
    p->configured=0;
    if(p->cor_changed){
        p->cor_changed=0;
        write_cfg(p,P2_CFG_COR,p->original_cor);
        if(!p->present)return P2_E_REMOVED;
        if(read_cfg(p,P2_CFG_COR)!=p->original_cor)return P2_E_VERIFY;
    }
    return P2_OK;
}
void p2_platform_close(struct p2_platform *p)
{
    if (!p) return;
    if (p->owned) {
        if (p->configured && p->present) {
            if (p->backup_only) {
                (void)p2_platform_finish_backup(p);
            } else {
                misc_control(&p->handle, 0);
                reset_card(&p->handle);
            }
        }
        release_card(&p->handle);
    }
    if (active_platform == p) active_platform = NULL;
    if (p->timer_open) CloseDevice((struct IORequest *)p->timer);
    if (p->timer) DeleteIORequest((struct IORequest *)p->timer);
    if (p->timer_port) DeleteMsgPort(p->timer_port);
    if (p->pccard) CloseLibrary(p->pccard);
    if (UtilityBase) { CloseLibrary((struct Library *)UtilityBase); UtilityBase = NULL; }
    free(p);
}
const char *p2_platform_description(void) { return "NATIVE AMIGA - hardware untested"; }
