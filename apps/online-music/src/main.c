/* SPDX-License-Identifier: MIT. Network and audio advance only on frame events. */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#include "catalog.h"
/* Unauthenticated first-party web endpoints; availability is not guaranteed.
 * Re-resolve each selection, accept only the URL actually granted by platform. */
#ifndef MUSIC_IDS
#define MUSIC_IDS "139774,480580003,441491"
#endif
#define CATALOG "http://music.163.com/api/song/detail/?ids=%5B" MUSIC_IDS "%5D"
#define TRACKS 8
#define BG ASTER_RGB565(12,18,30)
#define INK ASTER_RGB565(231,239,249)
#define DIM ASTER_RGB565(141,160,182)
#define ACCENT ASTER_RGB565(77,222,183)
static struct {char title[128];uint32_t id;} tracks[TRACKS];
static unsigned count, choice, frames, received, pending, pending_off, prefix_pos;
static int32_t request, audio;
static unsigned mode, next_mode=1, paused, eof, done;
static uint64_t request_started, last_draw;
static int error;
static unsigned touch_down,touch_moved,touch_x,touch_y;
static char url[1024];
static uint8_t bytes[2048];
static catalog_reader reader;
static aster_audio_stream_status_t playback;
static aster_http_status_t transfer;
static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[100];
static uint8_t arena[3072];
/* modes: 0 teardown, 1 catalogue, 2 MP3, 3 idle. Retain handles until close
 * accepts; never overwrite a still-owned TLS/audio session on next/refresh. */
static void change(unsigned target){next_mode=target;mode=0;pending=pending_off=0;last_draw=0;}
static void fail(const char *where,int rc)
{
    error=rc;aster_logf(ASTER_LOG_ERROR,"MUSIC error %s=%d received=%u",where,rc,received);change(3);
}
static void row(int y,const char *s,unsigned px,uint16_t color){aster_ge_text(&ge,16,y,px,color,0,s);}
static void draw(void)
{
    char b[128];int bottom=ge.h-68,rows=(bottom-126)/34;if(rows<1)rows=1;
    aster_ge_clear(&ge,BG);
    row(29,"在线音乐",24,INK);row(51,"网易云 · 公开可播曲目",12,DIM);
    row(69,"国内直连 · 128 kbps MP3",12,DIM);
    const char *label=error?"暂不可播放，点刷新重试":mode==1?"正在获取曲目…":mode==4?"正在获取播放地址…":mode==2?(paused?"已暂停":done?"播放完成":playback.state==ASTER_AUDIO_STREAM_BUFFERING?"正在缓冲…":"边下载边播放"):"选择歌曲开始播放";
    row(96,label,14,error?ASTER_RED:ACCENT);
    aster_snprintf(b,sizeof(b),"%u:%02u   已接收 %u KiB",playback.played_ms/60000,(playback.played_ms/1000)%60,received/1024);row(117,b,12,DIM);
    for(unsigned i=0;i<count&&i<(unsigned)rows;++i){
        int y=126+i*34;
        aster_ge_rect(&ge,10,y,ge.w-20,31,i==choice&&mode==2?ASTER_RGB565(24,65,64):ASTER_RGB565(21,31,47));
        /* Titles are clipped to the row width by the graphics text command. */
        char title[40];unsigned max=(ge.w-44)/8;if(max>=sizeof(title))max=sizeof(title)-1;
        size_t n=strlen(tracks[i].title);if(n>max)n=max;
        while(n&&((unsigned char)tracks[i].title[n]&0xc0)==0x80)--n;
        memcpy(title,tracks[i].title,n);title[n]=0;row(y+21,title,14,INK);
    }
    aster_ge_rect(&ge,10,bottom-9,ge.w-20,3,ASTER_RGB565(29,47,65));
    if(playback.capacity)aster_ge_rect(&ge,10,bottom-9,(ge.w-20)*playback.queued/playback.capacity,3,ACCENT);
    aster_ge_rect(&ge,10,bottom,ge.w-20,42,ASTER_RGB565(29,47,65));
    aster_ge_text(&ge,22,bottom+27,16,ACCENT,0,"上一首");
    aster_ge_text(&ge,ge.w/2-18,bottom+27,16,INK,0,paused?"继续":"暂停");
    aster_ge_text(&ge,ge.w-72,bottom+27,16,ACCENT,0,"下一首");
    aster_ge_text(&ge,16,ge.h-9,12,DIM,0,"刷新曲目");
    aster_ge_text(&ge,ge.w-84,ge.h-9,12,DIM,0,"停止播放");
    (void)aster_ge_present(&ge);
}
static void step(void)
{
    if(!mode){
        if(request>0){int rc=aster_http_close(request);if(rc==ASTER_EAGAIN)return;if(rc&&rc!=ASTER_EBADF){error=rc;return;}request=0;}
        if(audio>0){int rc=aster_audio_stream_close(audio);if(rc==ASTER_EAGAIN)return;if(rc&&rc!=ASTER_EBADF){error=rc;return;}audio=0;}
        mode=next_mode;eof=done=paused=0;received=0;memset(&playback,0,sizeof(playback));memset(&transfer,0,sizeof(transfer));
        if(mode==1||mode==4){if(mode==1)count=0;prefix_pos=0;memset(&reader,0,sizeof(reader));}
    }
    if(mode==3)return;
    if(!request&&!eof){
        const char *u=mode==1?CATALOG:url;
        int rc=aster_http_request(ASTER_HTTP_METHOD_GET,u,strlen(u),NULL,0,NULL,0,600000,mode==2?67108864:65536);
        if(rc==ASTER_EAGAIN||rc==ASTER_ENOSPC)return;if(rc<0){fail("request",rc);return;}request=rc;request_started=aster_time_now_ms();
    }
    if(mode==2&&!audio){
        int rc=aster_audio_stream_open(ASTER_AUDIO_STREAM_MP3);
        if(rc==ASTER_EAGAIN||rc==ASTER_EBUSY)return;if(rc<0){fail("audio open",rc);return;}audio=rc;
    }
    if(request){
        int rc=aster_http_status(request,&transfer,sizeof(transfer));
        if(rc==ASTER_EAGAIN)return;if(rc){fail("http status",rc);return;}
        if(!(transfer.flags&ASTER_HTTP_FLAG_HEADERS)&&aster_time_now_ms()-request_started>30000){fail("first byte timeout",ASTER_ETIMEDOUT);return;}
        if((transfer.flags&ASTER_HTTP_FLAG_HEADERS)&&transfer.status!=200){fail("HTTP",(int)transfer.status);return;}
    }
    if(audio){
        int rc=aster_audio_stream_status(audio,&playback,sizeof(playback));
        if(rc==ASTER_EAGAIN)return;if(rc){fail("audio status",rc);return;}
        if(playback.state==ASTER_AUDIO_STREAM_FAILED){fail("decoder",playback.error);return;}
        if(playback.state==ASTER_AUDIO_STREAM_DONE){if(!done)aster_logf(ASTER_LOG_INFO,"MUSIC done bytes=%u played=%u",received,playback.played_ms);done=1;return;}
    }
    /* At most six KiB per frame; preserve partially accepted MP3 writes. */
    for(unsigned budget=0;budget<3;++budget){
        if(mode==2&&pending>pending_off){
            int rc=aster_audio_stream_write(audio,bytes+pending_off,pending-pending_off);
            if(rc==ASTER_EAGAIN)return;if(rc<0){fail("audio write",rc);return;}pending_off+=rc;
            if(pending_off<pending)return;
            pending=pending_off=0;
        }
        if(eof){int rc=aster_audio_stream_finish(audio);if(rc==ASTER_EAGAIN)return;if(rc)fail("finish",rc);return;}
        int rc=aster_http_read(request,bytes,sizeof(bytes));
        if(rc==ASTER_EAGAIN)return;if(rc<0){fail("http read",rc);return;}
        if(!rc){
            if(mode!=2){fail("truncated JSON",ASTER_EPROTO);return;}
            eof=1;continue;
        }
        received+=rc;
        if(mode==2){pending=rc;continue;}
        for(int i=0;i<rc;++i){
            unsigned char ch=bytes[i];
            if(!reader.started){
                const char *prefix=mode==1?"{\"songs\":[":"{\"data\":[";
                if(ch==' '||ch=='\r'||ch=='\n'||ch=='\t')continue;
                if(ch!=prefix[prefix_pos++]){fail("JSON envelope",ASTER_EPROTO);return;}
                if(!prefix[prefix_pos])reader.started=1;
                continue;
            }
            if(!reader.depth&&ch==']'){
                if(mode==1&&count){aster_logf(ASTER_LOG_INFO,"MUSIC domestic catalog tracks=%u bytes=%u",count,received);change(3);}
                else fail("no playable track",ASTER_ENOTSUP);
                return;
            }
            int ready=catalog_byte(&reader,ch);
            if(ready<0){fail("JSON",ASTER_EPROTO);return;}
            if(!ready)continue;
            if(mode==4){
                char type[16];uint32_t code;const char *trial=catalog_property(reader.object,"freeTrialInfo");
                if(!catalog_uint(reader.object,"code",&code)||code!=200||!trial||strncmp(trial,"null",4)||( *json_space(trial+4)!=','&&*json_space(trial+4)!='}')||
                   !catalog_field(reader.object,"type",type,sizeof(type))||strcmp(type,"mp3")||
                   !catalog_field(reader.object,"url",url,sizeof(url))){fail("not freely playable MP3",ASTER_ENOTSUP);return;}
                const char *host=url; if(!strncmp(host,"http://",7))host+=7;else if(!strncmp(host,"https://",8))host+=8;else{fail("URL scheme",ASTER_EPROTO);return;}
                const char *end=host;while(*end&&*end!='/')++end;
                const char suffix[]=".music.126.net";size_t host_len=end-host;
                if(host_len<=sizeof(suffix)-1||memcmp(end-(sizeof(suffix)-1),suffix,sizeof(suffix)-1)){fail("CDN host",ASTER_EPROTO);return;}
                aster_logf(ASTER_LOG_INFO,"MUSIC resolved id=%u codec=mp3",tracks[choice].id);change(2);return;
            }
            char title[128];uint32_t id;
            if(!catalog_field(reader.object,"name",title,sizeof(title))||!catalog_uint(reader.object,"id",&id))continue;
            memcpy(tracks[count].title,title,strlen(title)+1);tracks[count].id=id;++count;
            if(count==TRACKS){change(3);return;}
        }
    }
}
static void play(unsigned i)
{
    if(i>=count)return;choice=i;error=0;
    aster_snprintf(url,sizeof(url),"http://music.163.com/api/song/enhance/player/url?ids=%%5B%u%%5D&br=128000",tracks[i].id);
    change(4);
}
int aster_app_init(void)
{
    aster_surface_desc_t d={sizeof(d),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
    aster_surface_buffer_t b={.size=sizeof(b),.version=1};int rc=aster_surface_create(&d,sizeof(d),&surface);if(rc)return rc;
    rc=aster_surface_acquire(surface,&b,sizeof(b));if(rc)return rc;
    aster_ge_init(&ge,surface,b.width,b.height,commands,100,arena,sizeof(arena));return ASTER_OK;
}
void aster_app_event(const aster_event_t *e)
{
    if(e->type==ASTER_EVENT_FRAME){step();
        /* This is a status/list screen, not a 30fps animation. Keep network
         * pumping every frame while limiting expensive full-panel rendering. */
        uint64_t now=aster_time_now_ms();
        if(!last_draw||now-last_draw>=200){draw();last_draw=now;}
        if(++frames%125==0&&audio>0)aster_logf(ASTER_LOG_INFO,"MUSIC streaming rx=%u total=%u played=%u queue=%u underruns=%u eof=%u",received,(unsigned)transfer.content_length,playback.played_ms,playback.queued,playback.underruns,eof);
    }
    if(e->type==ASTER_EVENT_TOUCH&&e->u.touch.action==ASTER_TOUCH_DOWN){touch_down=1;touch_moved=0;touch_x=e->u.touch.x;touch_y=e->u.touch.y;}
    if(e->type==ASTER_EVENT_TOUCH&&e->u.touch.action==ASTER_TOUCH_MOVE){
        int dx=(int)e->u.touch.x-(int)touch_x,dy=(int)e->u.touch.y-(int)touch_y;
        if(dx>12||dx< -12||dy>12||dy< -12)touch_moved=1;
    }
    if(e->type==ASTER_EVENT_TOUCH&&e->u.touch.action==ASTER_TOUCH_UP){
        unsigned clicked=touch_down&&!touch_moved;touch_down=0;if(!clicked)return;last_draw=0;
        unsigned y=e->u.touch.y,x=e->u.touch.x;
        if(y>ge.h-25){error=0;if(x>ge.w/2)change(3);else change(1);}
        else if(y>=ge.h-68){if(x<ge.w/3)play((choice+count-1)%(count?count:1));else if(x>ge.w*2/3)play((choice+1)%(count?count:1));else if(audio>0){int rc=paused?aster_audio_resume():aster_audio_pause();if(!rc)paused=!paused;}else if(count)play(choice);}
        else if(y>=126&&y<ge.h-68)play((y-126)/34);
    }
}
void aster_app_stop(void){if(request>0)(void)aster_http_close(request);if(audio>0)(void)aster_audio_stream_close(audio);}
