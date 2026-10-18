/* SPDX-License-Identifier: GPL-2.0-only */
#include "firmware.h"
#include "p2_stdio.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

const char p2_firmware_version_tag[] __attribute__((used)) =
    "\0$VER: Prism2Firmware " P2_AMIGA_VERSION " (" P2_AMIGA_DATE ") " P2_VERSION;
static void usage(void)
{
    puts("Prism2Firmware " P2_VERSION " - offline firmware inspection and validation\n"
         "  Prism2Firmware --version\n"
         "  Prism2Firmware inspect IMAGE\n"
         "  Prism2Firmware validate IMAGE --mode ram|flash --card SNAPSHOT --pda PDA\n"
         "                         [--primary PRIMARY_IMAGE]\n"
         "PDA must be a CRC-checked 1024-byte binary backup. See docs/OFFLINE-FIRMWARE.md.\n"
         "This utility reads files only. It does not access cards or write firmware.");
}
static void show(const struct p2_fw_image *im)
{
    size_t i;
    p2_printf("Component: id=0x%04x major=%u minor=%u variant=%u\n",im->component.id,im->component.major,im->component.minor,im->component.variant);
    p2_printf("Class: %s; entry=0x%08lx (separate from S3 load addresses)\n",im->entry ? "RAM" : "flash",(unsigned long)im->entry);
    p2_printf("Payload: %lu bytes in %lu segments; %lu S3 records\n",(unsigned long)im->payload_bytes,(unsigned long)im->segment_count,im->records);
    if (im->name[0]) p2_printf("Name: %s\n",im->name);
    if (im->has_sequence) p2_printf("Build sequence: %u\n",im->build_sequence);
    for (i=0;i<im->segment_count;++i)
        p2_printf("Segment: address=0x%08lx bytes=%lu\n",(unsigned long)im->segments[i].address,(unsigned long)im->segments[i].length);
    for (i=0;i<im->platform_count;++i) {
        const struct p2_identity *p=&im->platforms[i];
        p2_printf("Platform: id=0x%04x major=%u minor=%u variant=%u\n",p->id,p->major,p->minor,p->variant);
    }
    for (i=0;i<im->range_count;++i) {
        const struct p2_fw_range *r=&im->ranges[i];
        p2_printf("Interface: %s id=%u variant=%u bottom=%u top=%u\n",r->role ? "actor" : "supplier",r->interface_id,r->variant,r->bottom,r->top);
    }
    for (i=0;i<im->plug_count;++i) {
        const struct p2_fw_plug *p=&im->plugs[i];
        p2_printf("Plug: pdr=0x%08lx address=0x%08lx bytes=%lu\n",(unsigned long)p->pdr,(unsigned long)p->address,(unsigned long)p->length);
    }
    for (i=0;i<im->crc_count;++i) {
        const struct p2_fw_crc *r=&im->crcs[i];
        p2_printf("Signature descriptor: start=0x%08lx bytes=%lu program=%lu\n",(unsigned long)r->start,(unsigned long)r->length,(unsigned long)r->program);
    }
    puts("Signature descriptors are not computed CRC verification. Firmware bytes are unchanged.");
}
static int load(const char *path,struct p2_fw_image **image,struct p2_fw_error *error)
{
    FILE *file=fopen(path,"rb");
    int rc;
    if (!file) { p2_fprintf(stderr,"Cannot open image: %s\n",path); return P2_FW_IO; }
    rc=p2_fw_read(file,image,error); fclose(file);
    if (rc) p2_fprintf(stderr,"Image rejected: %s (line %lu): %s\n",path,error->line,error->message);
    return rc;
}
int main(int argc,char **argv)
{
    struct p2_fw_image *image=NULL,*primary=NULL;
    struct p2_fw_error error={0};
    struct p2_fw_card card;
    const char *card_path=NULL,*pda_path=NULL,*primary_path=NULL;
    enum p2_fw_mode mode=P2_FW_RAM;
    unsigned have_mode=0;
    uint8_t *pda=NULL;
    FILE *file;
    int i,rc=20;
    if (argc==2 && !strcmp(argv[1],"--version")) { puts("Prism2Firmware " P2_VERSION); return 0; }
    if (argc==2 && (!strcmp(argv[1],"--help") || !strcmp(argv[1],"help"))) { usage();return 0; }
    if (argc<3 || (strcmp(argv[1],"inspect") && strcmp(argv[1],"validate"))) { usage();return 20; }
    if (!strcmp(argv[1],"inspect") && argc!=3) { usage();return 20; }
    for (i=3;i<argc;i+=2) {
        if (i+1==argc) { usage();return 20; }
        if (!strcmp(argv[i],"--mode") && !have_mode) {
            if (!strcmp(argv[i+1],"ram")) mode=P2_FW_RAM;
            else if (!strcmp(argv[i+1],"flash")) mode=P2_FW_FLASH;
            else { usage();return 20; }
            have_mode=1;
        } else if (!strcmp(argv[i],"--card") && !card_path) card_path=argv[i+1];
        else if (!strcmp(argv[i],"--pda") && !pda_path) pda_path=argv[i+1];
        else if (!strcmp(argv[i],"--primary") && !primary_path) primary_path=argv[i+1];
        else { usage();return 20; }
    }
    if (!strcmp(argv[1],"validate") && (!have_mode || !card_path || !pda_path)) { usage();return 20; }
    if (load(argv[2],&image,&error)) goto done;
    if (!strcmp(argv[1],"inspect")) { show(image);puts("Structural inspection passed; card compatibility is not established.");rc=0;goto done; }
    file=fopen(card_path,"rb");
    if (!file) { p2_fprintf(stderr,"Cannot open card snapshot: %s\n",card_path);goto done; }
    i=p2_fw_card_read(file,&card,&error);fclose(file);
    if (i) { p2_fprintf(stderr,"Card snapshot rejected (line %lu): %s\n",error.line,error.message);goto done; }
    pda=malloc(P2_PDA_SIZE);
    if (!pda) { p2_fprintf(stderr,"Out of memory for PDA\n");goto done; }
    file=fopen(pda_path,"rb");
    if (!file) { p2_fprintf(stderr,"Cannot open PDA backup: %s\n",pda_path);goto done; }
    i=fread(pda,1,P2_PDA_SIZE,file)==P2_PDA_SIZE && fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if (!i) { p2_fprintf(stderr,"PDA backup must contain exactly 1024 bytes\n");goto done; }
    if (primary_path && load(primary_path,&primary,&error)) goto done;
    if (p2_fw_validate(image,mode,&card,pda,P2_PDA_SIZE,primary,&error)) {
        p2_fprintf(stderr,"Offline validation rejected: %s\n",error.message);goto done;
    }
    puts("Offline metadata and PDA checks passed. Firmware bytes are unchanged.\n"
         "Hardware profile/address-map approval and prepare/merge are still required.");rc=0;
 done:
    free(pda);p2_fw_free(primary);p2_fw_free(image);return rc;
}
