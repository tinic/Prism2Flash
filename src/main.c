/* SPDX-License-Identifier: GPL-2.0-only */
#include "platform.h"
#include "p2_stdio.h"
#include <string.h>

static void usage(void)
{
    puts("Prism2Genesis 0.1 - experimental Genesis RAM probe\n"
         "  Prism2Genesis info\n"
         "  Prism2Genesis inspect\n"
         "  Prism2Genesis probe --sram 8|16\n"
         "info reads CIS only; inspect initializes the card and reads firmware/PDA.\n"
         "probe resets the card and writes four RAM bytes, then restores normal mode.\n"
         "Select SRAM width from the exact card's hardware documentation.\n"
         "No firmware loading or flash programming is implemented in this milestone.");
}
static void show_id(const char *label, const struct p2_identity *id)
{
    p2_printf("%s: id=0x%04x variant=%u version=%u.%u\n", label,
           (unsigned)id->id, (unsigned)id->variant,
           (unsigned)id->major, (unsigned)id->minor);
}
int main(int argc, char **argv)
{
    struct p2_platform *platform = NULL;
    struct p2_card_info card;
    struct p2_bus bus;
    struct p2_identity nic, pri, sta;
    struct p2_probe_result probe;
    char error[192] = "";
    uint8_t pda[P2_PDA_SIZE];
    uint32_t pda_address = 0;
    unsigned bits = 0;
    int rc, cleanup_rc;
    int probing = argc > 1 && !strcmp(argv[1], "probe");
    int info = argc == 2 && !strcmp(argv[1], "info");
    int inspect = argc == 2 && !strcmp(argv[1], "inspect");
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "help"))) {
        usage(); return 0;
    }
    if (probing && argc == 4 && !strcmp(argv[2], "--sram")) {
        if (!strcmp(argv[3], "8")) bits = 8;
        else if (!strcmp(argv[3], "16")) bits = 16;
    }
    if (!info && !inspect && !(probing && bits)) { usage(); return 20; }
    p2_printf("Prism2Genesis 0.1 - %s\n", p2_platform_description());
    rc = p2_platform_open(&platform, &card, error, sizeof(error));
    if (rc) { p2_fprintf(stderr, "%s\n", error); return 20; }
    p2_printf("PCMCIA: manufacturer=0x%04x product=0x%04x\n",
           (unsigned)card.manufacturer, (unsigned)card.product);
    if (info) { p2_platform_close(platform); return 0; }
    rc = p2_platform_configure(platform, &bus, error, sizeof(error));
    if (rc) {
        p2_fprintf(stderr, "%s\n", error);
        p2_platform_close(platform); return 20;
    }
    rc = p2_initialize(&bus);
    if (!rc) rc = p2_identity(&bus, P2_RID_NICID, &nic);
    if (!rc) show_id("NIC", &nic);
    if (!rc && !p2_supported_nic(nic.id)) {
        p2_fprintf(stderr, "NIC is outside the initial HFA3842 profile list.\n");
        rc = P2_E_UNSUPPORTED;
    }
    if (!rc) rc = p2_identity(&bus, P2_RID_PRIID, &pri);
    if (!rc) show_id("PRI", &pri);
    if (!rc) {
        int station_rc = p2_identity(&bus, P2_RID_STAID, &sta);
        if (!station_rc) show_id("STA", &sta);
        else if (station_rc == P2_E_COMMAND)
            puts("STA: unavailable (primary-only firmware may be running)");
        else rc = station_rc;
    }
    if (!rc) rc = p2_read_pda(&bus, pda, &pda_address);
    if (!rc) p2_printf("PDA: validated 1024-byte read at 0x%08lx\n", (unsigned long)pda_address);
    if (!rc && probing) {
        p2_printf("Testing Genesis with %u-bit SRAM; no flash commands.\n", bits);
        rc = p2_genesis_probe(&bus, bits, &probe);
        p2_printf("COR before=0x%02x CCSR before=0x%02x readback=%02x %02x %02x %02x\n",
               probe.cor_before, probe.ccsr_before, probe.readback[0],
               probe.readback[1], probe.readback[2], probe.readback[3]);
        cleanup_rc = probe.cleanup_error;
        if (cleanup_rc) p2_fprintf(stderr, "Returning to normal mode: %s\n", p2_error_string(cleanup_rc));
        if (!rc) {
            /* Readback alone does not prove the original firmware restarted. */
            rc = p2_initialize(&bus);
            if (!rc) rc = p2_identity(&bus, P2_RID_PRIID, &pri);
            if (!rc) { show_id("PRI after reset", &pri); puts("Genesis RAM probe passed; normal firmware responded."); }
        }
    }
    if (rc) p2_fprintf(stderr, "Operation failed: %s\n", p2_error_string(rc));
    p2_platform_close(platform);
    return rc ? 20 : 0;
}
