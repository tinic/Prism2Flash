/* SPDX-License-Identifier: GPL-2.0-only */
#include "firmware.h"
#include <assert.h>
#include <string.h>

static const uint8_t input[] =
    "S307007E0002030471\n"
    "S30AFF2000000500010015BB\n"
    "S30CFF20000500020003000400C6\n"
    "S705003F0000BB\n"
    "S307007E0000010277\n";
int main(void)
{
    struct p2_fw_image *image=NULL;
    struct p2_fw_error error;
    static const uint8_t expected[]={1,2,3,4};
    uint8_t invalid_pda[P2_PDA_SIZE]={1,0,0,0,2,0,0,0,201,184};
    assert(p2_fw_parse(input,sizeof(input)-1,&image,&error)==P2_FW_OK);
    assert(image && image->entry==0x3f0000 && image->segment_count==1);
    assert(image->component.id==0x15 && image->component.variant==2);
    assert(image->component.major==3 && image->component.minor==4);
    assert(image->segments[0].address==0x7e0000 && image->segments[0].length==4);
    assert(!memcmp(image->segments[0].data,expected,4));
    p2_fw_free(image);
    image=(struct p2_fw_image *)1;
    assert(p2_fw_parse(NULL,1,&image,&error)==P2_FW_FORMAT && !image);
    assert(error.code==P2_FW_FORMAT && error.message[0]);
    assert(p2_fw_parse(NULL,0,&image,&error)==P2_FW_FORMAT && !image);
    assert(p2_fw_read(NULL,&image,&error)==P2_FW_IO && !image);
    assert(p2_fw_parse(input,sizeof(input)-1,NULL,&error)==P2_FW_FORMAT);
    assert(p2_validate_pda(invalid_pda)==P2_E_PDA);
    return 0;
}
