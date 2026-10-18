/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_STDIO_H
#define P2_STDIO_H
#include <stdio.h>
/* This program formats integers and strings only. Newlib's general floating
 * formatter otherwise opens mathieeedoubbas.library before main(), preventing
 * even --help from running on a bare installation. */
#ifdef PRISM2_AMIGA
#define p2_printf iprintf
#define p2_fprintf fiprintf
#define p2_snprintf sniprintf
#else
#define p2_printf printf
#define p2_fprintf fprintf
#define p2_snprintf snprintf
#endif
#endif
