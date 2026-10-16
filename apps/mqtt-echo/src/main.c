/* SPDX-License-Identifier: MIT. Event-driven bounded MQTT echo example. */
#include <aster/aster.h>
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef MQTT_HOST
#define MQTT_HOST "127.0.0.1"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_TLS
#define MQTT_TLS 0
#endif
static const char host[]=MQTT_HOST,client[]="aster-sdk-echo",topic[]="aster/example/echo";
static int32_t handle,operation;
static uint32_t stage,rounds,paused,last,connection;
static char payload[64];
static uint32_t payload_length;
static aster_mqtt_message_t message;
static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256];
static aster_string_t ref(const char *p,uint32_t n)
{ return (aster_string_t){(uint32_t)(uintptr_t)p,n}; }
static void fail(const char *where,int32_t code)
{ aster_logf(ASTER_LOG_ERROR,"MQTTAPP FAIL %s code=%d",where,code);aster_app_exit(1); }
static void draw(void)
{
    char label[80];
    aster_ge_clear(&ge,ASTER_RGB565(16,20,32));
    aster_ge_text(&ge,12,32,20,ASTER_WHITE,0,"MQTT Echo");
    aster_snprintf(label,sizeof(label),"State %u  rounds %u",connection,rounds);
    aster_ge_text(&ge,12,64,14,ASTER_CYAN,0,label);
    aster_ge_text(&ge,12,90,12,ASTER_WHITE,0,paused?"Tap: continue":"Tap: pause publishing");
    aster_ge_rect(&ge,(aster_time_now_ms()/15)%(ge.w-24),124,24,24,ASTER_GREEN);
    int rc=aster_ge_present(&ge);if(rc<0)fail("draw",rc);
}
int aster_app_init(void)
{
    aster_surface_desc_t desc={sizeof(desc),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
    aster_surface_buffer_t buffer={.size=sizeof(buffer),.version=1};
    int rc=aster_surface_create(&desc,sizeof(desc),&surface);if(rc)return rc;
    rc=aster_surface_acquire(surface,&buffer,sizeof(buffer));if(rc)return rc;
    if(buffer.width<=24)return ASTER_ENOTSUP;
    aster_ge_init(&ge,surface,buffer.width,buffer.height,commands,12,arena,sizeof(arena));
    last=aster_time_now_ms();return ASTER_OK;
}
void aster_app_event(const aster_event_t *e)
{
    if(e->type==ASTER_EVENT_TOUCH&&e->u.touch.action==ASTER_TOUCH_UP){paused=!paused;return;}
    if(e->type!=ASTER_EVENT_FRAME)return;
    draw();
    uint32_t now=aster_time_now_ms();
    if(!handle){
        aster_mqtt_options_t o={.size=sizeof(o),.version=1,
            .flags=ASTER_MQTT_FLAG_RECONNECT|(MQTT_TLS?ASTER_MQTT_FLAG_TLS:0),
            .port=MQTT_PORT,.timeout_ms=10000,.keepalive_s=10,
            .host=ref(host,sizeof(host)-1),.client_id=ref(client,sizeof(client)-1)};
        int rc=aster_mqtt_connect(&o,sizeof(o));if(rc==ASTER_EAGAIN)return;
        if(rc<0){fail("connect",rc);return;}handle=rc;
    }
    aster_mqtt_status_t s;
    int rc=aster_mqtt_status(handle,&s,sizeof(s));if(rc==ASTER_EAGAIN)return;
    if(rc){fail("status",rc);return;}connection=s.state;
    if(s.state>=ASTER_MQTT_STATE_FAILED){fail("connection",s.error);return;}
    if(s.state!=ASTER_MQTT_STATE_READY){last=now;return;}
    if(operation){
        int32_t error;rc=aster_mqtt_operation(handle,operation,&error);
        if(rc==ASTER_EAGAIN)return;
        if(rc){fail("operation query",rc);return;}
        operation=0;
        if(error){aster_logf(ASTER_LOG_WARN,"MQTTAPP operation failed=%d; connection=%u",error,s.state);stage=0;last=now;return;}
        ++stage;
    }
    if(stage==0){
        rc=aster_mqtt_subscribe(handle,topic,sizeof(topic)-1,1);
        if(rc==ASTER_EAGAIN||rc==ASTER_EBUSY)return;
        if(rc<0){fail("subscribe",rc);return;}operation=rc;return;
    }
    if(stage==1){
        if(paused||now-last<1000)return;
        payload_length=aster_snprintf(payload,sizeof(payload),"aster-message-%u",rounds);
        rc=aster_mqtt_publish(handle,topic,sizeof(topic)-1,payload,payload_length,1,0);
        if(rc==ASTER_EAGAIN)return;
        if(rc<0){fail("publish",rc);return;}operation=rc;last=now;return;
    }
    if(stage==2){
        rc=aster_mqtt_recv(handle,&message,sizeof(message));
        if(rc==ASTER_EAGAIN){if(now-last>15000)fail("echo timeout",ASTER_ETIMEDOUT);return;}
        if(rc){fail("recv",rc);return;}
        if(message.topic_length!=sizeof(topic)-1||memcmp(message.topic,topic,sizeof(topic)-1)||
            message.length!=payload_length||memcmp(message.data,payload,payload_length)){
            fail("content",ASTER_ECORRUPT);return;
        }
        ++rounds;stage=1;last=now;
        if(rounds==1||rounds%20==0)aster_logf(ASTER_LOG_INFO,"MQTTAPP round=%u qos=1 content=ok",rounds);
    }
}
void aster_app_stop(void) { if(handle>0)(void)aster_mqtt_close(handle); }
void aster_app_resume(void) { last=aster_time_now_ms(); }
