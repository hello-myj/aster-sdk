/* SPDX-License-Identifier: MIT: asynchronous event-driven TCP + UDP echo. */
#include <aster/aster.h>
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef NETWORK_HOST
#define NETWORK_HOST "127.0.0.1"
#endif
#ifndef NETWORK_TCP_PORT
#define NETWORK_TCP_PORT 17780
#endif
#ifndef NETWORK_UDP_PORT
#define NETWORK_UDP_PORT 17781
#endif
#ifndef NETWORK_REPEAT
#define NETWORK_REPEAT 1
#endif
#ifndef NETWORK_HOLD
#define NETWORK_HOLD 0
#endif
static const char host[] = NETWORK_HOST;
static int32_t tcp, udp;
static uint32_t sent, received, udp_stage, rounds, started, closing;
static uint32_t info_pending=1;
static uint32_t udp_wait, udp_retries, retries_total, stale;
static uint8_t body[1200], input[2048];
static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256];
static void draw(void)
{
    if(!surface)return;
    char label[80];
    aster_ge_clear(&ge,ASTER_RGB565(16,20,32));
    aster_ge_text(&ge,12,34,20,ASTER_WHITE,0,"Network Echo");
    aster_snprintf(label,sizeof(label),"TCP %u/512  UDP %u",received,udp_stage);
    aster_ge_text(&ge,12,68,14,ASTER_CYAN,0,label);
    aster_snprintf(label,sizeof(label),"Rounds %u",rounds);
    aster_ge_text(&ge,12,94,14,ASTER_WHITE,0,label);
    aster_ge_rect(&ge,(aster_time_now_ms()/15)%(ge.w-24),124,24,24,ASTER_GREEN);
    int rc=aster_ge_present(&ge);
    if(rc<0){aster_logf(ASTER_LOG_ERROR,"NETAPP draw=%d",rc);aster_app_exit(1);}
}
static void fail(const char *where, int32_t code)
{
    aster_logf(ASTER_LOG_ERROR,"NETAPP FAIL %s code=%d",where,code);
    aster_app_exit(1);
}
int aster_app_init(void)
{
    for (uint32_t i=0;i<sizeof(body);++i) body[i]=(uint8_t)i;
    started=aster_time_now_ms();
    if(aster_system_has_feature(ASTER_FEATURE_DISPLAY)>0){
        aster_surface_desc_t desc={sizeof(desc),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
        aster_surface_buffer_t buffer={.size=sizeof(buffer),.version=1};
        int rc=aster_surface_create(&desc,sizeof(desc),&surface);
        if(rc)return rc;
        rc=aster_surface_acquire(surface,&buffer,sizeof(buffer));if(rc)return rc;
        if(buffer.width<=24)return ASTER_ENOTSUP;
        aster_ge_init(&ge,surface,buffer.width,buffer.height,commands,12,arena,sizeof(arena));
    }
    return ASTER_OK;
}
static int ready(int32_t handle)
{
    aster_network_status_t status;
    int32_t rc=aster_network_status(handle,&status,sizeof(status));
    if(rc==ASTER_EAGAIN)return 0;
    if(rc){fail("status",rc);return 0;}
    if(status.state==ASTER_NETWORK_FAILED||status.state==ASTER_NETWORK_CANCELLED){fail("transport",status.error);return 0;}
    return status.state==ASTER_NETWORK_READY||status.state==ASTER_NETWORK_EOF;
}
void aster_app_event(const aster_event_t *event)
{
    if(event->type!=ASTER_EVENT_FRAME)return;
    draw();
    if(info_pending){
        aster_network_info_t info;int result=aster_network_get_info(&info,sizeof(info));
        if(result==ASTER_EAGAIN)return;
        if(result){fail("interface info",result);return;}
        aster_logf(ASTER_LOG_INFO,"NETAPP interface=%s flags=%u slots=%u",info.ipv4,info.flags,info.max_connections);
        info_pending=0;
    }
    if((uint32_t)(aster_time_now_ms()-started)>15000){fail("deadline",ASTER_ETIMEDOUT);return;}
    uint32_t now=aster_time_now_ms();
    if((udp_stage==1||udp_stage==2||udp_stage==4) && (uint32_t)(now-udp_wait)>1500){
        if(udp_retries==3){fail("udp retries",ASTER_ETIMEDOUT);return;}
        ++udp_retries;++retries_total;
        udp_stage=udp_stage==4?3:0;
        aster_logf(ASTER_LOG_WARN,"NETAPP udp retry=%u total=%u",udp_retries,retries_total);
    }
    if(closing){
        if(tcp>0){int rc=aster_network_close(tcp);if(rc==ASTER_EAGAIN)return;tcp=0;}
        if(udp>0){int rc=aster_network_close(udp);if(rc==ASTER_EAGAIN)return;udp=0;}
        aster_logf(ASTER_LOG_INFO,"NETAPP PASS rounds=%u retries=%u stale=%u",rounds,retries_total,stale);aster_app_exit(0);return;
    }
    if(!tcp){int rc=aster_network_open(ASTER_NETWORK_TCP,host,sizeof(host)-1,NETWORK_TCP_PORT,5000);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("tcp open",rc);return;}tcp=rc;aster_logf(ASTER_LOG_INFO,"NETAPP tcp=%d",tcp);}
    if(!udp){int rc=aster_network_open(ASTER_NETWORK_UDP,NULL,0,0,0);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("udp open",rc);return;}udp=rc;aster_logf(ASTER_LOG_INFO,"NETAPP udp=%d",udp);}
    if(!ready(tcp)||!ready(udp))return;
    if(sent<512){int rc=aster_network_send(tcp,body+sent,512-sent,NULL,0,0);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("tcp send",rc);return;}sent+=rc;}
    if(received<512){aster_network_packet_t packet;int rc=aster_network_recv(tcp,input,512-received,&packet,sizeof(packet));if(rc!=ASTER_EAGAIN){if(rc<=0||memcmp(input,body+received,rc)){fail("tcp bytes",rc);return;}received+=rc;}}
    if(udp_stage==0){
        /* Correlate retried binary datagrams; never count an old echo as new. */
        body[1196]=(uint8_t)rounds;body[1197]=(uint8_t)(rounds>>8);
        body[1198]=(uint8_t)(rounds>>16);body[1199]=(uint8_t)(rounds>>24);
        int rc=aster_network_send(udp,body,1200,host,sizeof(host)-1,NETWORK_UDP_PORT);if(rc==ASTER_EAGAIN)return;if(rc!=1200){fail("udp send",rc);return;}udp_stage=1;udp_wait=now;}
    if(udp_stage==1){aster_network_packet_t packet;int rc=aster_network_recv(udp,input,1199,&packet,sizeof(packet));if(rc==0){++stale;return;}if(rc!=ASTER_EAGAIN){if(rc!=ASTER_EMSGSIZE||packet.length!=1200){fail("udp short",rc);return;}udp_stage=2;}}
    if(udp_stage==2){aster_network_packet_t packet;int rc=aster_network_recv(udp,input,sizeof(input),&packet,sizeof(packet));if(rc==ASTER_EAGAIN)return;if(rc!=1200||packet.port!=NETWORK_UDP_PORT){fail("udp bytes",rc);return;}if(memcmp(input+1196,body+1196,4)){++stale;udp_stage=1;return;}if(memcmp(input,body,1200)){fail("udp bytes",rc);return;}udp_retries=0;udp_stage=3;}
    if(udp_stage==3){int rc=aster_network_send(udp,NULL,0,host,sizeof(host)-1,NETWORK_UDP_PORT);if(rc==ASTER_EAGAIN)return;if(rc){fail("udp empty send",rc);return;}udp_stage=4;udp_wait=now;}
    if(udp_stage==4){aster_network_packet_t packet;int rc=aster_network_recv(udp,NULL,0,&packet,sizeof(packet));if(rc==ASTER_EAGAIN)return;if(rc==ASTER_EMSGSIZE){rc=aster_network_recv(udp,input,sizeof(input),&packet,sizeof(packet));if(rc>=0){++stale;return;}if(rc==ASTER_EAGAIN)return;}if(rc){fail("udp empty recv",rc);return;}udp_retries=0;udp_stage=5;}
    if(received==512&&udp_stage==5){
        ++rounds;if(rounds==1||rounds%50==0)aster_logf(ASTER_LOG_INFO,"NETAPP round=%u tcp=512 udp=1200 empty=ok",rounds);
        if(NETWORK_HOLD){started=aster_time_now_ms();udp_stage=0;return;}
        if(rounds>=NETWORK_REPEAT){closing=1;return;}
        /* Reuse UDP for more rounds. TCP EOF is kept until final close. */
        udp_stage=0;started=aster_time_now_ms();
    }
}
void aster_app_stop(void)
{
    /* Best effort only: runtime also closes all session handles on stop/trap. */
    if(tcp>0)(void)aster_network_close(tcp);
    if(udp>0)(void)aster_network_close(udp);
}

void aster_app_resume(void) { started=aster_time_now_ms(); }
