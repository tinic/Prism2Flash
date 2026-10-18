/* SPDX-License-Identifier: GPL-2.0-only
 * Compatibility/PDA semantics adapted from Host AP utilities 0.4.7.
 * Deliberately no force option, default calibration fallback or hardware I/O.
 */
#include "firmware.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static int fail(struct p2_fw_error *e, int code, const char *message)
{
    if (e) { memset(e,0,sizeof(*e));e->code=code;strncpy(e->message,message,sizeof(e->message)-1); }
    return code;
}
static int same_id(const struct p2_identity *a, const struct p2_identity *b)
{ return a->id==b->id && a->variant==b->variant && a->major==b->major && a->minor==b->minor; }
static int same_range(const struct p2_fw_range *a, const struct p2_fw_range *b)
{ return a->role==b->role && a->interface_id==b->interface_id && a->variant==b->variant && a->bottom==b->bottom && a->top==b->top; }
static int fits(const struct p2_fw_image *im, uint32_t address, uint32_t length)
{
    size_t i;
    for (i=0;i<im->segment_count;++i) {
        const struct p2_fw_segment *s=&im->segments[i];
        if (address>=s->address && address-s->address<=s->length && length<=s->length-(address-s->address)) return 1;
    }
    return 0;
}
static const uint8_t *pdr(const uint8_t *data, unsigned type, size_t *length)
{
    size_t pos=0;
    while (pos+4<=P2_PDA_SIZE) {
        size_t words=le16(data+pos);
        unsigned id=le16(data+pos+2);
        if (!id) break;
        if (id==type) { *length=(words-1)*2; return data+pos+4; }
        pos+=(words+1)*2;
    }
    return NULL;
}
static int bind_pda(const uint8_t *data, size_t length, struct p2_fw_card *card, struct p2_fw_error *error)
{
    size_t pos=0,count=0,i,n=0;
    uint16_t seen[256];
    const uint8_t *value;
    struct p2_identity nic;
    if (!data || length!=P2_PDA_SIZE || p2_validate_pda(data)) return fail(error,P2_FW_PDA,"PDA must be a valid CRC-checked 1024-byte binary backup");
    while (pos+4<=length) {
        unsigned id=le16(data+pos+2);
        size_t words=le16(data+pos);
        if (!id) break;
        for (i=0;i<count;++i) if (seen[i]==id) return fail(error,P2_FW_PDA,"Duplicate PDR in PDA backup");
        if (count==sizeof(seen)/sizeof(seen[0])) return fail(error,P2_FW_PDA,"Too many PDRs");
        seen[count++]=(uint16_t)id; pos+=(words+1)*2;
    }
    value=pdr(data,8,&n);
    if (!value || n!=8) return fail(error,P2_FW_PDA,"PDA has no correctly sized NIC identity PDR");
    nic=(struct p2_identity){le16(value),le16(value+2),le16(value+4),le16(value+6)};
    if (!same_id(&nic,&card->nic)) return fail(error,P2_FW_INCOMPATIBLE,"PDA NIC identity differs from saved card identity");
    value=pdr(data,0x101,&n);
    if (!card->has_mac || !value || n!=6 || memcmp(value,card->mac,6))
        return fail(error,P2_FW_INCOMPATIBLE,"PDA MAC address differs from or is absent in saved card data");
    for (i=0;i<2;++i) {
        struct p2_fw_range r;
        value=pdr(data,(unsigned)(6+i),&n);
        if (!value || n!=10) return fail(error,P2_FW_PDA,"PDA lacks RF/MAC supplier range records");
        r=(struct p2_fw_range){le16(value),le16(value+2),le16(value+4),le16(value+6),le16(value+8)};
        if (r.role || r.interface_id!=i+1 || r.bottom>r.top) return fail(error,P2_FW_PDA,"PDA RF/MAC supplier range is malformed");
        if ((card->supplier_mask&(1u<<i)) && !same_range(&r,&card->suppliers[i])) return fail(error,P2_FW_INCOMPATIBLE,"Saved supplier range differs from PDA");
        card->suppliers[i]=r; card->supplier_mask|=1u<<i;
    }
    return P2_FW_OK;
}
static int image_check(const struct p2_fw_image *im, enum p2_fw_mode mode,
                       const struct p2_fw_card *card, const uint8_t *data,
                       struct p2_fw_error *error)
{
    size_t i,j,n;
    unsigned matched=0,actors=0;
    if (im->component.id!=0x15 && im->component.id!=0x1f) return fail(error,P2_FW_INCOMPATIBLE,"Only PRI and STA components are supported for validation");
    if ((mode==P2_FW_FLASH && im->entry) || (mode==P2_FW_RAM && !im->entry)) return fail(error,P2_FW_INCOMPATIBLE,"Image RAM/flash class differs from selected mode");
    for (i=0;i<im->platform_count;++i) if (same_id(&im->platforms[i],&card->nic)) matched=1;
    if (!matched) return fail(error,P2_FW_INCOMPATIBLE,"Exact NIC id/variant/major/minor is absent from image platforms");
    for (i=0;i<im->segment_count;++i) {
        const struct p2_fw_segment *s=&im->segments[i];
        if ((s->address&1) || (s->length&1) || s->address>=UINT32_C(0x800000) || s->length>UINT32_C(0x800000)-s->address)
            return fail(error,P2_FW_INCOMPATIBLE,"Payload exceeds current even-aligned 23-bit AUX transport bounds");
    }
    /* Keep Host AP's conservative per-record range checks. No variant bypass
     * when validating a replacement primary+station pair. */
    for (i=0;i<im->range_count;++i) {
        const struct p2_fw_range *a=&im->ranges[i],*b;
        if (!a->role) continue;
        ++actors;
        if (!(card->supplier_mask&(1u<<(a->interface_id-1)))) return fail(error,P2_FW_INCOMPATIBLE,"Saved card data lacks a required supplier interface");
        b=&card->suppliers[a->interface_id-1];
        if (a->variant!=b->variant || a->top<b->bottom || a->bottom>b->top) return fail(error,P2_FW_INCOMPATIBLE,"Actor/supplier interface variant or range mismatch");
    }
    if (!actors) return fail(error,P2_FW_INCOMPATIBLE,"Image has no actor compatibility records");
    for (i=0;i<im->plug_count;++i) {
        const struct p2_fw_plug *plug=&im->plugs[i];
        if (!fits(im,plug->address,plug->length)) return fail(error,P2_FW_INCOMPATIBLE,"PDA plug destination is outside image payload");
        for (j=0;j<i;++j) {
            const struct p2_fw_plug *other=&im->plugs[j];
            if (plug->address<other->address+other->length && other->address<plug->address+plug->length)
                return fail(error,P2_FW_INCOMPATIBLE,"Overlapping PDA plug destinations");
        }
        if (plug->pdr==UINT32_MAX) continue; /* reserved filename field; no card calibration */
        if (!pdr(data,(unsigned)plug->pdr,&n)) return fail(error,P2_FW_PDA,"Required plug PDR is missing; defaults are not accepted");
        if (n!=plug->length) return fail(error,P2_FW_PDA,"PDA plug length differs from image requirement");
    }
    for (i=0;i<im->crc_count;++i) {
        const struct p2_fw_crc *c=&im->crcs[i];
        if ((c->start&1) || (c->length&1) || c->start>=UINT32_C(0x800000) || c->length>UINT32_C(0x800000)-c->start)
            return fail(error,P2_FW_INCOMPATIBLE,"Signature descriptor exceeds AUX bounds");
        if (!fits(im,c->start,1) || !fits(im,c->start+c->length-1,1))
            return fail(error,P2_FW_INCOMPATIBLE,"Signature descriptor endpoints are outside payload");
        if (c->program) {
            int begins=0;
            for (j=0;j<im->segment_count;++j) if (im->segments[j].address==c->start) begins=1;
            if (!begins) return fail(error,P2_FW_INCOMPATIBLE,"Signature region does not begin at a payload segment");
            for (j=0;j<im->plug_count;++j)
                if (im->plugs[j].address<c->start && im->plugs[j].address+im->plugs[j].length>c->start-2)
                    return fail(error,P2_FW_INCOMPATIBLE,"Signature stamp would overwrite a PDA plug");
        }
    }
    return P2_FW_OK;
}
static int hex_digit(unsigned char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
int p2_fw_validate(const struct p2_fw_image *im, enum p2_fw_mode mode,
                   const struct p2_fw_card *input, const uint8_t *pda, size_t length,
                   const struct p2_fw_image *primary, struct p2_fw_error *error)
{
    struct p2_fw_card card;
    size_t i,j;
    unsigned found=0;
    int rc;
    if (error) memset(error,0,sizeof(*error));
    if (!im || !input || (mode!=P2_FW_RAM && mode!=P2_FW_FLASH)) return fail(error,P2_FW_FORMAT,"Invalid validation arguments");
    card=*input;
    if ((rc=bind_pda(pda,length,&card,error))!=0) return rc;
    if (primary) {
        if (primary->component.id!=0x15 || im->component.id!=0x1f) return fail(error,P2_FW_INCOMPATIBLE,"Replacement pair must be PRI plus STA");
        if ((rc=image_check(primary,mode,&card,pda,error))!=0) return rc;
        for (i=0;i<primary->range_count;++i) {
            const struct p2_fw_range *r=&primary->ranges[i];
            if (!r->role && r->interface_id==3) {
                if (found++) return fail(error,P2_FW_INCOMPATIBLE,"Ambiguous replacement PRI supplier range");
                card.suppliers[2]=*r; card.supplier_mask|=4;
            }
        }
        if (!found) return fail(error,P2_FW_INCOMPATIBLE,"Replacement primary has no PRI supplier range");
        for (i=0;i<primary->segment_count;++i) for (j=0;j<im->segment_count;++j) {
            const struct p2_fw_segment *a=&primary->segments[i],*b=&im->segments[j];
            if (a->address<b->address+b->length && b->address<a->address+a->length)
                return fail(error,P2_FW_INCOMPATIBLE,"Primary/station payload ranges overlap");
        }
    }
    return image_check(im,mode,&card,pda,error);
}
int p2_fw_card_read(FILE *file, struct p2_fw_card *card, struct p2_fw_error *error)
{
    char line[256];
    unsigned long line_no=0;
    unsigned header=0,nic=0;
    if (!file || !card) return fail(error,P2_FW_FORMAT,"Invalid card snapshot arguments");
    memset(card,0,sizeof(*card));
    if (error) memset(error,0,sizeof(*error));
    memset(line,0xff,sizeof(line));
    while (fgets(line,sizeof(line),file)) {
        char *tokens[7],*token;
        unsigned long numbers[4];
        size_t count=0,i,len=strlen(line);
        ++line_no;
        if (line_no>P2_FW_MAX_LINES) goto bad;
        /* fgets may contain an embedded NUL. Locate its actual terminator in
         * a pre-filled buffer rather than accepting a hidden trailing field. */
        if ((!strchr(line,'\n') && !feof(file)) || !len) goto bad;
        for (i=len+1;i<sizeof(line);++i) if ((unsigned char)line[i]!=255) goto bad;
        token=strtok(line," \t\r\n");
        if (!token || token[0]=='#') { memset(line,0xff,sizeof(line));continue; }
        while (token) {
            if (count==7) goto bad;
            tokens[count++]=token; token=strtok(NULL," \t\r\n");
        }
        if (!header) {
            if (count!=2 || strcmp(tokens[0],"P2CARD") || strcmp(tokens[1],"1")) goto bad;
            header=1;memset(line,0xff,sizeof(line));continue;
        }
        if (count==2 && !strcmp(tokens[0],"MAC")) {
            unsigned nonzero=0;
            if (card->has_mac || strlen(tokens[1])!=17) goto bad;
            for (i=0;i<6;++i) {
                int hi=hex_digit((unsigned char)tokens[1][i*3]);
                int lo=hex_digit((unsigned char)tokens[1][i*3+1]);
                if (hi<0 || lo<0 || (i<5 && tokens[1][i*3+2]!=':')) goto bad;
                card->mac[i]=(uint8_t)((hi<<4)|lo); nonzero|=card->mac[i];
            }
            if (!nonzero || (card->mac[0]&1)) goto bad;
            card->has_mac=1;memset(line,0xff,sizeof(line));continue;
        }
        if (count!=5 || (strcmp(tokens[0],"NIC") && strcmp(tokens[0],"SUP"))) goto bad;
        for (i=0;i<4;++i) {
            char *end;
            int base=10;
            if (tokens[i+1][0]=='-' || tokens[i+1][0]=='+') goto bad;
            if (tokens[i+1][0]=='0' && (tokens[i+1][1]=='x' || tokens[i+1][1]=='X')) base=16;
            errno=0; numbers[i]=strtoul(tokens[i+1],&end,base);
            if (errno || !*tokens[i+1] || *end || numbers[i]>65535ul) goto bad;
        }
        if (!strcmp(tokens[0],"NIC")) {
            if (nic++) goto bad;
            card->nic=(struct p2_identity){(uint16_t)numbers[0],(uint16_t)numbers[1],(uint16_t)numbers[2],(uint16_t)numbers[3]};
        } else {
            unsigned iface=(unsigned)numbers[0];
            if (!iface || iface>4 || numbers[2]>numbers[3] || (card->supplier_mask&(1u<<(iface-1)))) goto bad;
            card->suppliers[iface-1]=(struct p2_fw_range){0,(uint16_t)iface,(uint16_t)numbers[1],(uint16_t)numbers[2],(uint16_t)numbers[3]};
            card->supplier_mask|=1u<<(iface-1);
        }
        memset(line,0xff,sizeof(line));
    }
    if (ferror(file)) return fail(error,P2_FW_IO,"Card snapshot read error");
    if (header && nic && card->has_mac) return P2_FW_OK;
 bad:
    fail(error,P2_FW_FORMAT,"Invalid, duplicate or incomplete card snapshot field");
    if (error) error->line=line_no;
    memset(card,0,sizeof(*card)); return P2_FW_FORMAT;
}
