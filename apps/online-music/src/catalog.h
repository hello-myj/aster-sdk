/* SPDX-License-Identifier: MIT. Bounded incremental JSON object collector.
 * Consume the upstream array without retaining the complete music catalogue. */
#ifndef ONLINE_CATALOG_H
#define ONLINE_CATALOG_H
#include <stdint.h>
#ifdef __wasm__
#include <aster_libc.h>
#else
#include <string.h>
#endif
#include <stddef.h>
typedef struct { char object[8192]; unsigned used, depth, quoted, escaped, started; } catalog_reader;
/* 1 complete object, -1 oversized/invalid, 0 more input needed. */
static int catalog_byte(catalog_reader *r, unsigned char c)
{
    if(!r->depth){
        if(c=='{'){r->used=0;r->quoted=r->escaped=0;r->depth=1;r->object[r->used++]=c;return 0;}
        if(c=='['&&!r->started){r->started=1;return 0;}
        if(c==' '||c=='\r'||c=='\n'||c=='\t'||c==','||c==']')return 0;
        return -1;
    }
    if(r->used+1>=sizeof(r->object))return -1;
    r->object[r->used++]=c;
    if(r->quoted){if(r->escaped)r->escaped=0;else if(c=='\\')r->escaped=1;else if(c=='"')r->quoted=0;}
    else if(c=='"')r->quoted=1;
    else if(c=='{'){if(++r->depth>16)return -1;}
    else if(c=='}'&&!--r->depth){r->object[r->used]=0;return 1;}
    return 0;
}
static const char *json_space(const char *p){while(*p==' '||*p=='\n'||*p=='\t'||*p=='\r')++p;return p;}
static int json_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int json_unicode(const char **p,unsigned *cp)
{
    unsigned v=0;for(unsigned i=0;i<4;++i){int n=json_hex(*(*p)++);if(n<0)return 0;v=v*16+(unsigned)n;}*cp=v;return 1;
}
static const char *json_string(const char *p,char *out,size_t cap)
{
    if(*p++!='"')return NULL;size_t n=0;
    while(*p&&*p!='"'){
        unsigned c=(unsigned char)*p++;unsigned char encoded[4];size_t count=1;encoded[0]=c;
        if(c<32)return NULL;
        if(c=='\\'){
            c=(unsigned char)*p++;if(!c)return NULL;
            if(c=='u'){
                if(strlen(p)<4||!json_unicode(&p,&c))return NULL;
                if(c>=0xd800&&c<=0xdbff){unsigned low;if(p[0]!='\\'||p[1]!='u'||strlen(p+2)<4)return NULL;p+=2;if(!json_unicode(&p,&low)||low<0xdc00||low>0xdfff)return NULL;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}
                else if(c>=0xdc00&&c<=0xdfff)return NULL;
                if(c<32)return NULL;
                if(c<128){encoded[0]=c;count=1;}
                else if(c<2048){encoded[0]=0xc0|(c>>6);encoded[1]=0x80|(c&63);count=2;}
                else if(c<65536){encoded[0]=0xe0|(c>>12);encoded[1]=0x80|((c>>6)&63);encoded[2]=0x80|(c&63);count=3;}
                else{encoded[0]=0xf0|(c>>18);encoded[1]=0x80|((c>>12)&63);encoded[2]=0x80|((c>>6)&63);encoded[3]=0x80|(c&63);count=4;}
            }else{
                if(c!='"'&&c!='\\'&&c!='/'){if(out||!(c=='n'||c=='r'||c=='t'||c=='b'||c=='f'))return NULL;}encoded[0]=c;
            }
        }
        if(out){if(n+count>=cap)return NULL;memcpy(out+n,encoded,count);}n+=count;
    }
    if(*p!='"')return NULL;if(out)out[n]=0;return p+1;
}
/* Extract an immediate string property; nested data and escaped key text are
 * never mistaken for filenames. Unknown values are skipped, not interpreted. */
static const char *catalog_property(const char *p,const char *wanted)
{
    if(*p++!='{')return 0;
    for(;;){
        char key[64];p=json_space(p);if(*p=='}')return 0;
        const char *next=json_string(p,key,sizeof(key));if(!next)return 0;
        p=json_space(next);if(*p++!=':')return 0;p=json_space(p);
        if(!strcmp(key,wanted))return p;
        unsigned depth=0;
        while(*p){
            if(*p=='"'){p=json_string(p,NULL,0);if(!p)return 0;continue;}
            if(*p=='['||*p=='{')++depth;
            if(*p==']'||*p=='}'){if(!depth)break;--depth;}
            if(*p==','&&!depth)break;++p;
        }
        if(*p!=',')return 0;++p;
    }
}
static int catalog_field(const char *p,const char *key,char *out,size_t cap)
{
    p=catalog_property(p,key);return p&&json_string(p,out,cap)!=NULL;
}
static int catalog_uint(const char *p,const char *key,uint32_t *out)
{
    p=catalog_property(p,key);if(!p||*p<'0'||*p>'9')return 0;
    uint32_t n=0;while(*p>='0'&&*p<='9'){unsigned d=*p++-'0';if(n>(UINT32_MAX-d)/10)return 0;n=n*10+d;}
    p=json_space(p);if(*p!=','&&*p!='}')return 0;*out=n;return 1;
}
#endif
