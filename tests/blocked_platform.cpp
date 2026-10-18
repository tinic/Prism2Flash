/* SPDX-License-Identifier: GPL-2.0-only */
#include "platform.h"
#include <stdlib.h>

/* Any backend call means the mandatory-backup gate was reached too late. */
int p2_platform_open(struct p2_platform **p, struct p2_card_info *i,
                     char *error, size_t size)
{
    (void)p; (void)i; (void)error; (void)size;
    abort();
}
int p2_platform_configure(struct p2_platform *p, struct p2_bus *b,
                         char *error, size_t size)
{
    (void)p; (void)b; (void)error; (void)size;
    abort();
}
void p2_platform_close(struct p2_platform *p) { (void)p; abort(); }
const char *p2_platform_description(void) { abort(); }
int p2_platform_configure_backup(struct p2_platform *p,struct p2_bus *b,char *error,size_t size)
{ (void)p;(void)b;(void)error;(void)size;abort(); }
int p2_platform_backup_directory(const char *path) { (void)path;abort(); }
int p2_platform_finish_backup(struct p2_platform *p) { (void)p;abort(); }
