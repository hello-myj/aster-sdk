/* SPDX-License-Identifier: MIT. N2 native ABI/lifecycle fixture, not a demo. */
#include <aster_imports.h>
#ifndef HTTP_URL
#define HTTP_URL "https://127.0.0.1:9446/stall"
#endif
#ifndef HTTP_PROBE_MODE
#define HTTP_PROBE_MODE 0
#endif
static int32_t handle;
static uint32_t started;
static const char url[]=HTTP_URL;
static void failed(const char *stage,int error)
{
    aster_logf(ASTER_LOG_ERROR,"HTTPPROBE FAIL %s error=%d",stage,error);
    aster_app_exit(1);
}
int aster_app_init(void)
{
    started=aster_time_now_ms();
    if(HTTP_PROBE_MODE==4){
        handle=aster_http_request(1,url,sizeof(url)-1,NULL,0,NULL,0,30000,65536);
        if(handle<=0)return handle;
        aster_logf(ASTER_LOG_INFO,"HTTPPROBE INIT_CLEANUP handle=%d",handle);
        return ASTER_EIO;
    }
    return ASTER_OK;
}
void aster_app_event(const aster_event_t *event)
{
    if(event->type!=ASTER_EVENT_FRAME)return;
    if((uint32_t)(aster_time_now_ms()-started)>12000){failed("deadline",ASTER_ETIMEDOUT);return;}
    if(!handle){
        int rc=aster_http_request(1,url,sizeof(url)-1,NULL,0,NULL,0,30000,65536);
        if(rc==ASTER_EAGAIN)return;
        if(HTTP_PROBE_MODE==1){
            if(rc!=ASTER_EPERM){failed("permission",rc);return;}
            aster_logf(ASTER_LOG_INFO,"HTTPPROBE PASS permission");aster_app_exit(0);return;
        }
        if(rc<=0){failed("request",rc);return;}handle=rc;
        if(HTTP_PROBE_MODE==0){
            aster_http_status_t s;
            if(aster_http_status(handle,&s,sizeof(s)-1)!=ASTER_EINVAL||
               aster_http_status(handle,(void *)(uintptr_t)0xfffffff8u,sizeof(s))!=ASTER_EINVAL||
               aster_http_read(handle,(void *)(uintptr_t)0xfffffff8u,32)!=ASTER_EINVAL||
               aster_http_read(handle,(void *)(uintptr_t)128,0)!=ASTER_EINVAL){failed("bounds",ASTER_EINVAL);return;}
            aster_logf(ASTER_LOG_INFO,"HTTPPROBE PASS bounds");aster_app_exit(0);return;
        }
    }
    aster_http_status_t s;int rc=aster_http_status(handle,&s,sizeof(s));
    if(rc==ASTER_EAGAIN)return;if(rc){failed("status",rc);return;}
    if(s.state>=ASTER_HTTP_STATE_FAILED){failed("transport",s.error);return;}
    if(HTTP_PROBE_MODE==2&&s.state==ASTER_HTTP_STATE_HANDSHAKE){
        aster_logf(ASTER_LOG_INFO,"HTTPPROBE TRAP handshake");__builtin_trap();
    }
    if(HTTP_PROBE_MODE==3&&s.state==ASTER_HTTP_STATE_BODY&&s.queued){
        aster_logf(ASTER_LOG_INFO,"HTTPPROBE PASS exit-body queued=%u",s.queued);
        aster_app_exit(0);
    }
}
/* Deliberately no close: runtime must clean up exit, trap and init failure. */
void aster_app_stop(void) {}
