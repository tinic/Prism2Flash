/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "platform.h"
#include "sha256.h"
#include "p2_stdio.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>
#ifdef PRISM2_AMIGA
#define P2_CAPTURE_SIMULATION "false"
#else
#define P2_CAPTURE_SIMULATION "true"
#endif

int p2_backup_valid_utc(const char *s)
{
    unsigned i,y,m,d,h,n,z,days;
    static const unsigned mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (!s || strlen(s)!=16 || s[8]!='T' || s[15]!='Z')return 0;
    for(i=0;i<15;++i)if(i!=8 && (s[i]<'0'||s[i]>'9'))return 0;
    y=(s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
    m=(s[4]-'0')*10+s[5]-'0';d=(s[6]-'0')*10+s[7]-'0';
    h=(s[9]-'0')*10+s[10]-'0';n=(s[11]-'0')*10+s[12]-'0';z=(s[13]-'0')*10+s[14]-'0';
    if(y<2020||y>2099||m<1||m>12||h>23||n>59||z>59)return 0;
    days=mdays[m-1]+(m==2 && (y%4==0 && (y%100!=0 || y%400==0)));
    return d>=1 && d<=days;
}
static int available(struct p2_bus *b)
{
    if(!b->present(b->ctx))return P2_E_REMOVED;
    return b->cancelled && b->cancelled(b->ctx) ? P2_E_CANCELLED : P2_OK;
}
static int stable_chunk(struct p2_bus *b,uint32_t address,uint8_t *out,
                        uint32_t start,struct p2_capture_result *result)
{
    uint8_t next[128];unsigned page,attempt;int rc;
    for(page=0;page<4096;page+=128){
        for(attempt=0;attempt<20;++attempt){
            if((uint32_t)(b->now_us(b->ctx)-start)>=UINT32_C(240000000))return P2_E_TIMEOUT;
            rc=p2_aux_read_nv(b,address+page,attempt?next:out+page,128);
            if(rc)return rc;
            ++result->page_reads;
            if(attempt){
                if(!memcmp(out+page,next,128))break;
                ++result->page_disagreements;
                memcpy(out+page,next,128);
            }
        }
        /* Require two consecutive whole-page reads, never vote/repair bytes. */
        if(attempt==20)return P2_E_VERIFY;
    }
    return P2_OK;
}
int p2_capture_nv(struct p2_bus *b,FILE *first,FILE *second,struct p2_capture_result *out)
{
    static const uint16_t saved_regs[]={P2_PARAM0,P2_PARAM1,P2_PARAM2,P2_AUXPAGE,P2_AUXOFFSET};
    uint16_t control;
    uint8_t *buffers;
    uint32_t address,start;
    unsigned pass,i;
    int rc,opened=0,cleanup=0,changed=0;
    int (*cancel_fn)(void *);
    struct p2_sha256 hash;
    FILE *files[2]={first,second};
    if(!b || !first || !second || !out || first==second)return P2_E_ARGUMENT;
    memset(out,0,sizeof(*out));
    buffers=malloc(8192);if(!buffers)return P2_E_IO;
    rc=available(b);if(rc){free(buffers);return rc;}
    start=b->now_us(b->ctx);control=b->read16(b->ctx,P2_CONTROL);
    out->before[0]=control;
    rc=available(b);if(rc){free(buffers);return rc;}
    /* Refuse an absent/stuck bus and any enable/disable transition. */
    if(control==0xffff || ((control&0xc000)!=0 && (control&0xc000)!=0xc000)){
        free(buffers);return P2_E_IO;
    }
    for(i=0;i<5;++i){
        rc=available(b);if(rc){free(buffers);return rc;}
        out->before[i+1]=b->read16(b->ctx,saved_regs[i]);
    }
    rc=available(b);if(rc){free(buffers);return rc;}
    /* Exclusive slot ownership also covers firmware that leaves AUX enabled.
     * Do not toggle it in that case; only disable a session opened here. */
    if(!(control&0xc000)){opened=1;rc=p2_aux_enable(b,1);}
    for(pass=0;!rc && pass<2;++pass){
        if(pass && (fflush(first)||fseek(first,0,SEEK_SET))) {rc=P2_E_IO;break;}
        p2_sha256_init(&hash);
        for(address=0;!rc && address<P2_NV_BYTES;address+=4096){
            rc=available(b);if(rc)break;
            if((uint32_t)(b->now_us(b->ctx)-start)>=UINT32_C(240000000)){rc=P2_E_TIMEOUT;break;}
            rc=stable_chunk(b,address,buffers,start,out);if(rc)break;
            if(pass){
                size_t got=fread(buffers+4096,1,4096,first);
                out->comparison_read_bytes=(uint32_t)got;
                if(got!=4096){rc=P2_E_IO;break;}
                for(i=0;i<4096;++i)if(buffers[i]!=buffers[i+4096]){
                    if(!out->mismatch_bytes)out->first_mismatch=address+i;
                    ++out->mismatch_bytes;
                }
            }
            if(fwrite(buffers,1,4096,files[pass])!=4096){rc=P2_E_IO;break;}
            for(i=1;i<4096;++i)if(buffers[i]!=buffers[0])changed=1;
            p2_sha256_update(&hash,buffers,4096);out->bytes[pass]+=4096;
            if((address&0xfffff)==0){p2_printf("NV pass %u: %lu / %lu bytes\n",pass+1,(unsigned long)out->bytes[pass],(unsigned long)P2_NV_BYTES);fflush(stdout);}
        }
        if(!rc && fflush(files[pass]))rc=P2_E_IO;
        if(!rc)p2_sha256_hex(&hash,out->sha256[pass]);
    }
    if(!rc && (!changed || out->mismatch_bytes || strcmp(out->sha256[0],out->sha256[1])))rc=P2_E_VERIFY;
    /* Cleanup cannot be vetoed by Ctrl-C, but must never touch another card. */
    cancel_fn=b->cancelled;b->cancelled=NULL;
    /* linux-wlan-ng clears AUXOFFSET's selector while AUX is enabled;
     * Host AP disables AUX with PARAM0/1/2 zero. These are scratch registers,
     * not persistent settings. Never replay old values after disabling AUX. */
    if(b->present(b->ctx))b->write16(b->ctx,P2_AUXOFFSET,0);
    if(opened)cleanup=p2_aux_enable(b,0);
    if(!b->present(b->ctx) && !cleanup)cleanup=P2_E_REMOVED;
    if(b->present(b->ctx)){
        out->after[0]=b->read16(b->ctx,P2_CONTROL);
        if((out->after[0]&0xc000)!=(control&0xc000) && !cleanup)cleanup=P2_E_VERIFY;
        for(i=0;i<5 && b->present(b->ctx);++i){
            out->after[i+1]=b->read16(b->ctx,saved_regs[i]);
        }
        if(!b->present(b->ctx) && !cleanup)cleanup=P2_E_REMOVED;
    }
    b->cancelled=cancel_fn;
    out->cleanup_error=cleanup;out->elapsed_us=b->now_us(b->ctx)-start;
    free(buffers);
    return rc ? rc : cleanup;
}
static int verify_file(const char *name,const char *expected)
{
    FILE *file=fopen(name,"rb");
    struct p2_sha256 hash;
    uint8_t *buf;char actual[65];size_t n;uint32_t total=0;int rc=P2_E_IO;
    if(!file)return rc;
    buf=malloc(4096);if(!buf){fclose(file);return rc;}
    p2_sha256_init(&hash);
    while((n=fread(buf,1,4096,file))!=0){
        if(n>P2_NV_BYTES-total)goto done;
        total+=(uint32_t)n;p2_sha256_update(&hash,buf,n);
    }
    if(ferror(file)||total!=P2_NV_BYTES)goto done;
    p2_sha256_hex(&hash,actual);rc=strcmp(actual,expected)?P2_E_VERIFY:P2_OK;
done:
    free(buf);if(fclose(file))rc=P2_E_IO;return rc;
}
int p2_backup_command(const char *directory,const char *utc)
{
    struct p2_platform *platform=NULL;
    struct p2_card_info card={0};struct p2_bus bus;
    struct p2_capture_result result={0};
    char error[192]="",paths[3][384];
    FILE *files[2]={NULL,NULL},*manifest;
    unsigned i;int rc,verify=0,configured=0;
    if(!p2_backup_valid_utc(utc)||!strstr(directory,utc)||strlen(directory)>320){
        p2_fprintf(stderr,"Use a new persistent directory containing the supplied YYYYMMDDTHHMMSSZ UTC timestamp.\n");return 20;
    }
    if(p2_platform_backup_directory(directory)){
        p2_fprintf(stderr,"Cannot create a NEW backup directory; existing directories and RAM/T are refused.\n");return 20;
    }
    for(i=0;i<3;++i)p2_snprintf(paths[i],sizeof(paths[i]),"%s/%s",directory,i==0?"nv-read-1.bin":i==1?"nv-read-2.bin":"manifest.json");
    files[0]=fopen(paths[0],"w+b");files[1]=fopen(paths[1],"w+b");
    rc=files[0]&&files[1]?P2_OK:P2_E_IO;
    if(!rc)rc=p2_platform_open(&platform,&card,error,sizeof(error));
    if(!rc){rc=p2_platform_configure_backup(platform,&bus,error,sizeof(error));if(!rc)configured=1;}
    if(!rc)rc=p2_capture_nv(&bus,files[0],files[1],&result);
    if(configured){result.backend_cleanup_error=p2_platform_finish_backup(platform);if(result.backend_cleanup_error && !rc)rc=result.backend_cleanup_error;}
    p2_platform_close(platform);
    for(i=0;i<2;++i)if(files[i] && fclose(files[i]) && !rc)rc=P2_E_IO;
    if(result.bytes[0]==P2_NV_BYTES && result.bytes[1]==P2_NV_BYTES){
        int file_rc=verify_file(paths[0],result.sha256[0]);
        if(!file_rc)file_rc=verify_file(paths[1],result.sha256[1]);
        if(!file_rc)verify=1;else if(!rc)rc=file_rc;
    }
    manifest=fopen(paths[2],"wb");
    if(manifest){
        p2_fprintf(manifest,"{\n  \"format\": \"Prism2 AUX NV capture 1\",\n  \"tool_version\": \"" P2_VERSION "\",\n"
          "  \"utc_supplied\": \"%s\",\n  \"timestamp_basis\": \"operator-supplied UTC; host must retain actual start/end and build-info.json\",\n"
          "  \"simulation_only\": " P2_CAPTURE_SIMULATION ",\n"
          "  \"cis_manufacturer\": %u,\n  \"cis_product\": %u,\n  \"space\": \"NV\",\n  \"selector\": 1,\n"
          "  \"address_start\": 0,\n  \"address_end_exclusive\": 8388608,\n  \"byte_order\": \"little-endian word bytes\",\n"
          "  \"bytes_read\": [%lu, %lu],\n  \"sha256\": [\"%s\", \"%s\"],\n  \"elapsed_us\": %lu,\n"
          "  \"cleanup_error\": %d,\n  \"error\": %d,\n  \"nv_address_space_complete\": %s,\n"
          "  \"backend_cleanup_error\": %d,\n"
          "  \"page_verification\": \"two consecutive identical 128-byte reads; maximum twenty reads per page per pass\",\n"
          "  \"page_reads\": %lu,\n  \"page_disagreements\": %lu,\n"
          "  \"mismatch_bytes\": %lu,\n  \"first_mismatch\": %lu,\n  \"comparison_read_bytes\": %lu,\n"
          "  \"register_order\": [\"CONTROL\",\"PARAM0\",\"PARAM1\",\"PARAM2\",\"AUXPAGE\",\"AUXOFFSET\"],\n"
          "  \"registers_before\": [%u,%u,%u,%u,%u,%u],\n  \"registers_after\": [%u,%u,%u,%u,%u,%u],\n"
          "  \"files_reopened_and_verified\": %s,\n  \"physical_flash_coverage_verified\": false,\n"
          "  \"authorizes_impactful_operation\": false\n}\n",utc,card.manufacturer,card.product,
          (unsigned long)result.bytes[0],(unsigned long)result.bytes[1],result.sha256[0],result.sha256[1],
          (unsigned long)result.elapsed_us,result.cleanup_error,rc,rc?"false":"true",result.backend_cleanup_error,
          (unsigned long)result.page_reads,(unsigned long)result.page_disagreements,
          (unsigned long)result.mismatch_bytes,(unsigned long)result.first_mismatch,(unsigned long)result.comparison_read_bytes,
          result.before[0],result.before[1],result.before[2],result.before[3],result.before[4],result.before[5],
          result.after[0],result.after[1],result.after[2],result.after[3],result.after[4],result.after[5],verify?"true":"false");
        if(ferror(manifest))rc=P2_E_IO;
        if(fclose(manifest))rc=P2_E_IO;
    }else rc=P2_E_IO;
    if(rc)p2_fprintf(stderr,"NV capture failed: %s; %s. Keep partial files and manifest.\n",p2_error_string(rc),error);
    else p2_printf("Two complete AUX NV reads match byte-for-byte; saved files reopened and SHA-256 verified.\n"
                   "SHA-256: %s\nPhysical flash coverage/profile must still be established; active operations remain blocked.\n",result.sha256[0]);
    return rc?20:0;
}
