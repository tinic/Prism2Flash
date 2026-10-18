/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "sha256.h"
#include "mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static void hash_tests(void)
{
    struct p2_sha256 s;char out[65];unsigned i;
    p2_sha256_init(&s);p2_sha256_hex(&s,out);
    CHECK(!strcmp(out,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    p2_sha256_init(&s);p2_sha256_update(&s,&"xa"[1],1);p2_sha256_update(&s,"bc",2);p2_sha256_hex(&s,out);
    CHECK(!strcmp(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    p2_sha256_init(&s);for(i=0;i<1000000;++i)p2_sha256_update(&s,"a",1);p2_sha256_hex(&s,out);
    CHECK(!strcmp(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
static void pda_oracle_test(void)
{
    /* Original synthetic prefix; 0xe1a3 independently calculated using the
     * retained Host AP util.c CRC table, not the fixture generator. */
    uint8_t pda[P2_PDA_SIZE]={2,0,8,0,0x1b,0x80,2,0,0,0,0xa3,0xe1};
    CHECK(!p2_validate_pda(pda));
    pda[10]^=1;CHECK(p2_validate_pda(pda)==P2_E_PDA);
}
static void init_nv(struct p2_mock *m)
{
    uint32_t i;
    CHECK(!p2_mock_init(m,0));m->nv_memory=malloc(P2_NV_BYTES);CHECK(m->nv_memory);
    for(i=0;i<P2_NV_BYTES;++i)m->nv_memory[i]=(uint8_t)(i*17+(i>>8)*3);
}
static void transfer_tests(void)
{
    struct p2_mock m;uint8_t b[260];unsigned ops;
    init_nv(&m);
    CHECK(!p2_aux_read_nv(&m.bus,126,b,sizeof(b)));
    CHECK(!memcmp(b,m.nv_memory+126,sizeof(b)) && m.aux_space==1);
    CHECK(!p2_aux_read_nv(&m.bus,0x7ffffe,b,2));CHECK(!memcmp(b,m.nv_memory+0x7ffffe,2));
    ops=m.operations;
    CHECK(p2_aux_read_nv(&m.bus,1,b,2)==P2_E_ARGUMENT);
    CHECK(p2_aux_read_nv(&m.bus,0x7ffffe,b,4)==P2_E_ARGUMENT);
    CHECK(p2_aux_read_nv(&m.bus,0x800000,b,0)==P2_E_ARGUMENT);
    CHECK(p2_aux_read_nv(&m.bus,0,b,3)==P2_E_ARGUMENT);
    CHECK(m.operations==ops && !m.payload_writes && !m.command_writes && !m.configuration_writes);
    p2_mock_free(&m);
}
static void capture_tests(int enabled,int mismatch)
{
    struct p2_mock m;struct p2_capture_result result;FILE *a=tmpfile(),*b=tmpfile();
    uint16_t regs[5]={0x1234,0x5678,0xabcd,0x4444,0x2020};
    unsigned i;static const uint16_t regids[]={P2_PARAM0,P2_PARAM1,P2_PARAM2,P2_AUXPAGE,P2_AUXOFFSET};
    CHECK(a && b);init_nv(&m);
    m.regs[P2_CONTROL/2]=enabled?0xc000:0;
    for(i=0;i<5;++i)m.regs[regids[i]/2]=regs[i];
    if(mismatch)m.corrupt_nv_after=P2_NV_BYTES;
    else m.corrupt_nv_once_at=5;
    CHECK(p2_capture_nv(&m.bus,a,b,&result)==(mismatch?P2_E_VERIFY:P2_OK));
    CHECK(result.bytes[0]==P2_NV_BYTES);
    CHECK(result.bytes[1]==P2_NV_BYTES);
    CHECK((result.mismatch_bytes!=0)==(mismatch!=0));
    if(!mismatch)CHECK(result.page_disagreements==1 && result.page_reads==4*(P2_NV_BYTES/128)+1);
    if(!mismatch)CHECK(strlen(result.sha256[0])==64 && !strcmp(result.sha256[0],result.sha256[1]));
    CHECK(!result.cleanup_error && m.regs[P2_CONTROL/2]==(enabled?0xc000:0));
    for(i=0;i<3;++i)CHECK(m.regs[regids[i]/2]==(enabled?regs[i]:0));
    CHECK(m.regs[P2_AUXOFFSET/2]==0 && !m.scratch_writes_after_disable);
    CHECK(!m.command_writes && !m.payload_writes && !m.configuration_writes && !m.flash_commands);
    fclose(a);fclose(b);p2_mock_free(&m);
}
static void failure_tests(void)
{
    struct p2_mock m;struct p2_capture_result result;FILE *a,*b;unsigned i;
    for(i=1;i<140;i+=9){
        CHECK(!p2_mock_init(&m,0));m.remove_at=i;a=tmpfile();b=tmpfile();CHECK(a&&b);
        CHECK(p2_capture_nv(&m.bus,a,b,&result)==P2_E_REMOVED);
        CHECK(!m.command_writes && !m.payload_writes && !m.configuration_writes);
        fclose(a);fclose(b);p2_mock_free(&m);
    }
    CHECK(!p2_mock_init(&m,0));m.cancel_at=60;a=tmpfile();b=tmpfile();CHECK(a&&b);
    CHECK(p2_capture_nv(&m.bus,a,b,&result)==P2_E_CANCELLED);
    CHECK(!result.cleanup_error && !m.command_writes && !m.payload_writes);
    fclose(a);fclose(b);p2_mock_free(&m);
    CHECK(!p2_mock_init(&m,0));m.stuck_aux=1;a=tmpfile();b=tmpfile();CHECK(a&&b);
    CHECK(p2_capture_nv(&m.bus,a,b,&result)==P2_E_TIMEOUT);
    CHECK(result.cleanup_error==P2_E_TIMEOUT && m.clock<300000 && !m.command_writes && !m.payload_writes);
    fclose(a);fclose(b);p2_mock_free(&m);
    init_nv(&m);m.unstable_nv=1;a=tmpfile();b=tmpfile();CHECK(a&&b);
    CHECK(p2_capture_nv(&m.bus,a,b,&result)==P2_E_VERIFY);
    CHECK(result.page_reads==20 && result.page_disagreements==19 && !result.bytes[0]);
    CHECK(!result.cleanup_error && !m.command_writes && !m.payload_writes);
    fclose(a);fclose(b);p2_mock_free(&m);
}
int main(void)
{
    hash_tests();pda_oracle_test();transfer_tests();capture_tests(0,0);capture_tests(1,0);capture_tests(0,1);failure_tests();
    CHECK(p2_backup_valid_utc("20261018T104000Z"));CHECK(p2_backup_valid_utc("20240229T235959Z"));
    CHECK(!p2_backup_valid_utc("20260229T104000Z"));CHECK(!p2_backup_valid_utc("20261018T246000Z"));
    CHECK(!p2_backup_valid_utc("20261018T104000Z\n"));
    puts("SHA-256 vectors, NV bounds, full reads, mismatch/removal/cancel/timeout and canonical AUX cleanup passed");
    return 0;
}
