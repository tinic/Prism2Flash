/* SPDX-License-Identifier: GPL-2.0-only */
#include "platform.h"
#include "mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
struct p2_platform { struct p2_mock mock; };
int p2_platform_open(struct p2_platform **out, struct p2_card_info *info, char *error, size_t size)
{
    *out = calloc(1, sizeof(**out));
    if (!*out) { snprintf(error, size, "Out of memory"); return P2_E_IO; }
    /* A protocol model, not a hardware emulator or evidence of card support. */
    *info = (struct p2_card_info){0x0274, 0x1613, 0x3e0, 0x100, 1};
    return P2_OK;
}
int p2_platform_configure(struct p2_platform *p, struct p2_bus *bus, char *error, size_t size)
{
    int rc = p2_mock_init(&p->mock, 0); /* Model accepts either explicitly selected width. */
    (void)error; (void)size;
    if (!rc) *bus = p->mock.bus;
    return rc;
}
void p2_platform_close(struct p2_platform *p)
{
    if (p) { p2_mock_free(&p->mock); free(p); }
}
const char *p2_platform_description(void) { return "SIMULATION ONLY - no hardware access"; }
int p2_platform_configure_backup(struct p2_platform *p,struct p2_bus *bus,char *error,size_t size)
{
    uint32_t i;
    int rc=p2_platform_configure(p,bus,error,size);
    if(rc)return rc;
    p->mock.nv_memory=malloc(0x800000);
    if(!p->mock.nv_memory)return P2_E_IO;
    memset(p->mock.nv_memory,0xff,0x800000);
    for(i=0x7e0000;i<0x800000;++i)p->mock.nv_memory[i]=(uint8_t)(i*17+(i>>8)*3);
    return P2_OK;
}
int p2_platform_backup_directory(const char *path) { return mkdir(path,0700)?P2_E_IO:P2_OK; }
int p2_platform_finish_backup(struct p2_platform *p) { (void)p;return P2_OK; }
