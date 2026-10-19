/* SPDX-License-Identifier: MIT. Event-driven HTTP/HTTPS, no blocking waits. */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef HTTP_URL
#define HTTP_URL "http://127.0.0.1:9080/json"
#endif
#ifndef HTTP_DOWNLOAD
#define HTTP_DOWNLOAD 0
#endif
static const char url[] = HTTP_URL;
static const char output[] = "/data/download.bin";
static int32_t request;
static aster_handle_t surface, file;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256], buffer[512];
static char json[257];
static uint32_t total, done, failed, close_pending;
static uint32_t crc = 0xffffffffu;
static void fail(const char *where, int32_t error)
{
    if (failed) return;
    failed = 1;
    aster_logf(ASTER_LOG_ERROR, "HTTPAPP FAIL %s code=%d bytes=%u", where, error, total);
    if (file) { (void)aster_fs_close(file); file = 0; }
    if (HTTP_DOWNLOAD) (void)aster_fs_unlink(output, sizeof(output)-1);
    if (request > 0) (void)aster_http_cancel(request);
    close_pending = request > 0;
}
int aster_app_init(void)
{
    aster_surface_desc_t desc={sizeof(desc),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
    aster_surface_buffer_t b={.size=sizeof(b),.version=1};
    int rc=aster_surface_create(&desc,sizeof(desc),&surface); if(rc)return rc;
    rc=aster_surface_acquire(surface,&b,sizeof(b)); if(rc)return rc;
    aster_ge_init(&ge,surface,b.width,b.height,commands,12,arena,sizeof(arena));
    if(HTTP_DOWNLOAD){
        rc=aster_fs_open(output,sizeof(output)-1,ASTER_FS_O_CREAT|ASTER_FS_O_TRUNC|ASTER_FS_O_WRONLY,&file);
        if(rc)return rc;
    }
    return ASTER_OK;
}
static void draw(void)
{
    char label[80];
    aster_ge_clear(&ge,ASTER_RGB565(16,20,32));
    aster_ge_text(&ge,12,34,20,ASTER_WHITE,0,HTTP_DOWNLOAD?"HTTP Download":"HTTP Fetch");
    aster_snprintf(label,sizeof(label),"%s: %u bytes",failed?"Failed":done?"Complete":"Receiving",total);
    aster_ge_text(&ge,12,68,14,failed?ASTER_RED:ASTER_CYAN,0,label);
    if(!HTTP_DOWNLOAD&&done)aster_ge_text(&ge,12,96,12,ASTER_WHITE,0,json);
    aster_ge_rect(&ge,(aster_time_now_ms()/15)%(ge.w-24),124,24,24,ASTER_GREEN);
    if(aster_ge_present(&ge)<0)fail("draw",ASTER_EIO);
}
void aster_app_event(const aster_event_t *event)
{
    if(event->type==ASTER_EVENT_SYS && event->u.sys.code==ASTER_SYS_NET_STATE){
        aster_logf(ASTER_LOG_INFO,"HTTPAPP NETSTATE handle=%u kind=%u state=%u",
                   event->u.sys.value,event->u.data[2],event->u.data[3]);
        return;
    }
    if(event->type!=ASTER_EVENT_FRAME)return;
    draw();
    if(close_pending){
        int rc=aster_http_close(request);if(rc==ASTER_EAGAIN)return;
        if(rc){fail("close",rc);return;} request=0;close_pending=0;
    }
    if(done||failed)return;
    if(!request){
        int rc=aster_http_request(ASTER_HTTP_METHOD_GET,url,sizeof(url)-1,NULL,0,NULL,0,60000,HTTP_DOWNLOAD?65536:256);
        if(rc==ASTER_EAGAIN)return;if(rc<0){fail("request",rc);return;}request=rc;
    }
    aster_http_status_t status;
    int rc=aster_http_status(request,&status,sizeof(status));
    if(rc==ASTER_EAGAIN)return;if(rc){fail("status",rc);return;}
    if((status.flags&ASTER_HTTP_FLAG_HEADERS)&&status.status!=200){fail("HTTP status",(int)status.status);return;}
    /* One bounded read and one bounded file write per frame. */
    rc=aster_http_read(request,buffer,sizeof(buffer));
    if(rc==ASTER_EAGAIN)return;if(rc<0){fail("read",rc);return;}
    if(rc>0){
        if(HTTP_DOWNLOAD){
            for(int i=0;i<rc;++i){
                if(buffer[i]!=(uint8_t)((total+(uint32_t)i)%251)){fail("pattern",ASTER_ECORRUPT);return;}
                crc^=buffer[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
            }
            int written=aster_fs_write(file,buffer,(uint32_t)rc);
            if(written!=rc){fail("file write",written<0?written:ASTER_EIO);return;}
        }else{
            if(total+(uint32_t)rc>=sizeof(json)){fail("JSON limit",ASTER_EMSGSIZE);return;}
            memcpy(json+total,buffer,(uint32_t)rc);json[total+(uint32_t)rc]=0;
        }
        total+=(uint32_t)rc;return;
    }
    /* EOF alone is insufficient: verify the application fixture and headers. */
    rc=aster_http_status(request,&status,sizeof(status));
    if(rc==ASTER_EAGAIN)return;if(rc||status.state!=ASTER_HTTP_STATE_DONE){fail("final status",rc?rc:ASTER_EPROTO);return;}
    if(HTTP_DOWNLOAD){
        if(total!=65536){fail("length",ASTER_ECORRUPT);return;}
        rc=aster_fs_close(file);file=0;if(rc){fail("file close",rc);return;}
    }else{
        const char expected[]="{\"ok\":true,\"value\":42}\n";
        char type[64];uint32_t required;
        rc=aster_http_header(request,"Content-Type",12,0,type,sizeof(type),&required);
        if(rc==ASTER_EAGAIN)return;
        if(rc!=16||memcmp(type,"application/json",16)||total!=sizeof(expected)-1||memcmp(json,expected,total)){
            fail("JSON fixture",ASTER_EPROTO);return;
        }
    }
    done=1;close_pending=1;
    aster_logf(ASTER_LOG_INFO,"HTTPAPP PASS mode=%s bytes=%u crc32=%08x",HTTP_DOWNLOAD?"download":"json",total,crc^0xffffffffu);
}
void aster_app_stop(void)
{
    if(request>0)(void)aster_http_close(request);
    if(file){(void)aster_fs_close(file);file=0;}
    if(HTTP_DOWNLOAD&&!done)(void)aster_fs_unlink(output,sizeof(output)-1);
}
