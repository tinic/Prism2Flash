/* SPDX-License-Identifier: GPL-2.0-only
 * Bare-drive environment for native executable smoke tests.
 */
#include <proto/dos.h>
#include <dos/dos.h>
#include <stddef.h>
#include <string.h>
#include "sha256.h"

int main(void)
{
    static const char *const names[] = {"ENV", "ENVARC", "T", "CLIPS", "LIBS", "DEVS"};
    static const char *const paths[] = {"DH0:env", "DH0:envarc", "DH0:t", "DH0:clips", "DH0:libs", "DH0:devs"};
    unsigned i;
    struct p2_sha256 hash;
    char digest[65];
    p2_sha256_init(&hash);p2_sha256_update(&hash,"abc",3);p2_sha256_hex(&hash,digest);
    if(strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))return RETURN_FAIL;
    for (i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        BPTR lock = Lock((CONST_STRPTR)paths[i], SHARED_LOCK);
        if (!lock) return RETURN_FAIL;
        if (!AssignLock((CONST_STRPTR)names[i], lock)) {
            UnLock(lock);
            return RETURN_FAIL;
        }
    }
    return RETURN_OK;
}
