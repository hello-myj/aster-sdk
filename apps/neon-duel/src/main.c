#include <aster_imports.h>
#include <aster_gfx_engine.h>
#include "game.h"
#include "art.h"

static aster_handle_t surface;
static uint32_t width,height,previous,accumulator,frames;
static uint16_t canvas[320*240], lettering[240*8];
static aster_gfx_t gfx;
static Game game;
static int32_t sounds[4];
static uint32_t slots[8],keys;
static int suspended;
#define COLOR(r,g,b) ASTER_RGB565(r,g,b)
static const uint16_t white=COLOR(217,241,244),cyan=COLOR(34,217,233),pink=COLOR(239,73,125);
static void rectangle(int x,int y,int w,int h,uint16_t c)
{
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    if(x+w>320)w=320-x;if(y+h>240)h=240-y;
    if(w>0&&h>0)aster_gfx_fill_rect(&gfx,x,y,w,h,c);
}
static void text(int x,int y,const char *s,uint16_t color,int scale)
{
    aster_gfx_t stamp;
    memset(lettering,0,sizeof(lettering));aster_gfx_init(&stamp,lettering,240,8,480);
    uint32_t n=aster_gfx_text(&stamp,0,0,s,color,0,0);if(n>240)n=240;
    for(int yy=0;yy<7;yy++)for(uint32_t xx=0;xx<n;xx++)if(lettering[yy*240+xx])rectangle(x+xx*scale,y+yy*scale,scale,scale,color);
}
static void centered(int y,const char *s,uint16_t c,int scale){text((320-(int)strlen(s)*6*scale)/2,y,s,c,scale);}
static void panel(int x,int y,int w,int h,uint16_t accent)
{
    rectangle(x,y,w,h,COLOR(13,20,34));rectangle(x,y,w,1,accent);
    rectangle(x,y+h-1,w,1,COLOR(62,79,101));rectangle(x,y,1,h,COLOR(62,79,101));rectangle(x+w-1,y,1,h,COLOR(62,79,101));
}
static void sprite(int fighter,int pose,int cx,int feet,int facing)
{
    int x0=cx-26,y0=feet-69,position=0,index=fighter*9+pose;
    for(uint32_t i=art_offsets[index];i<art_offsets[index+1];i+=2){
        int length=art_runs[i],color=art_runs[i+1];
        if(color)for(int k=0;k<length;k++){
            int p=position+k,x=x0+(facing>0?p%52:51-p%52),y=y0+p/52;
            if(x>=0&&x<320&&y>=0&&y<240)canvas[y*320+x]=art_palette[color];
        }
        position+=length;
    }
}
static void fighter(int who)
{
    Fighter *p=&game.p[who];int x=(int)p->x,y=183-(int)p->y;
    aster_gfx_disk(&gfx,x,184,13,COLOR(12,17,30));
    sprite(who,game_pose(p,frames),x,y,p->attack?p->attack_dir:p->facing);
    if(p->guarding){
        int side=x+p->facing*21;
        aster_gfx_line(&gfx,side,145,side+p->facing*3,159,cyan);
        aster_gfx_line(&gfx,side+p->facing*3,159,side,174,cyan);
    }
}
static void hud(void)
{
    char s[32];
    panel(7,6,125,29,cyan);panel(188,6,125,29,pink);
    text(12,10,"KAI / YOU",white,1);text(230,10,game.training?"BOT / DUMMY":"REI / CPU",white,1);
    rectangle(12,22,115,7,COLOR(39,51,67));rectangle(193,22,115,7,COLOR(39,51,67));
    rectangle(12,22,game.p[0].hp*115/100,7,cyan);rectangle(308-game.p[1].hp*115/100,22,game.p[1].hp*115/100,7,pink);
    rectangle(12,37,game.p[0].energy*94/100,3,game.p[0].energy>=60?COLOR(255,211,92):COLOR(14,120,152));
    rectangle(308-game.p[1].energy*94/100,37,game.p[1].energy*94/100,3,COLOR(153,52,117));
    text(12,43,game.p[0].energy>=60?"BURST READY":"CHARGING",COLOR(128,163,185),1);
    panel(139,6,42,29,COLOR(255,211,92));
    if(game.training)text(145,16,"TRAIN",white,1);
    else{int seconds=(game.round_ms+999)/1000;s[0]='0'+seconds/10;s[1]='0'+seconds%10;s[2]=0;text(148,13,s,white,2);}
    for(int i=0;i<2;i++){
        aster_gfx_disk(&gfx,120+i*8,44,2,game.wins[0]>i?cyan:COLOR(57,64,80));
        aster_gfx_disk(&gfx,192+i*8,44,2,game.wins[1]>i?pink:COLOR(57,64,80));
    }
    aster_snprintf(s,sizeof(s),"R%u",(uint32_t)game.round);text(154,40,s,COLOR(146,163,184),1);
    if(game.p[0].combo>1&&game.p[0].combo_ms>0){aster_snprintf(s,sizeof(s),"%u HIT COMBO",(uint32_t)game.p[0].combo);text(12,60,s,COLOR(255,211,92),1);}
}
static const int button_x[7]={5,49,93,133,177,223,269};
static const int button_w[7]={40,40,36,40,42,42,46};
static const char *button_labels[7]={"<",">","UP","BLOCK","PUNCH","KICK","BURST"};
static void controls(void)
{
    rectangle(0,191,320,49,COLOR(10,17,29));
    for(int i=0;i<7;i++){
        uint32_t bit=1u<<i;int active=(game.held&bit)!=0;
        uint16_t accent=i<4?cyan:i==6?COLOR(255,211,92):pink;
        int disabled=i==6&&game.p[0].energy<60;
        if(disabled)accent=COLOR(83,90,103);
        panel(button_x[i],195,button_w[i],40,accent);
        if(active)rectangle(button_x[i]+2,197,button_w[i]-4,36,COLOR(45,64,82));
        const char *label=button_labels[i];int scale=i<2?2:1;
        text(button_x[i]+(button_w[i]-(int)strlen(label)*6*scale)/2,207,label,accent,scale);
        if(i==6)text(button_x[i]+14,223,"60",disabled?accent:white,1);
    }
}
static void overlay(void)
{
    char s[40];
    if(game.mode==MENU){
        panel(34,46,252,137,cyan);
        centered(59,"NEON DUEL",white,3);centered(88,"ROOFTOP RIVALS",cyan,1);
        centered(104,"ORIGINAL PIXEL FIGHTER",COLOR(141,162,185),1);
        panel(58,125,96,35,pink);panel(166,125,96,35,cyan);
        text(76,137,"FIGHT",pink,2);text(184,137,"TRAIN",cyan,2);
        centered(169,"FIRST TO 2 WINS / 90 SECONDS",COLOR(141,162,185),1);
        rectangle(0,191,320,49,COLOR(10,17,29));
        centered(200,"TOUCH BUTTONS OR USE YOUR KEYBOARD",white,1);
        centered(214,"ARROWS MOVE/JUMP  Z PUNCH  X KICK",COLOR(141,162,185),1);
        centered(227,"DOWN BLOCK  C BURST  P PAUSE",COLOR(141,162,185),1);
    }else if(game.mode==PAUSED){
        panel(57,77,206,76,cyan);centered(89,"PAUSED",white,3);
        centered(121,"TAP TO RESUME",cyan,1);centered(136,"ESC: RETURN TO TITLE",COLOR(141,162,185),1);
    }else if(game.mode==ROUND_OVER||game.mode==MATCH_OVER){
        panel(44,75,232,92,game.winner==0?cyan:pink);
        centered(87,game.winner==2?"DRAW":game.winner==0?"YOU WIN":"REI WINS",white,3);
        aster_snprintf(s,sizeof(s),"%s  %u - %u",game.mode==MATCH_OVER?"MATCH":"ROUND",(uint32_t)game.wins[0],(uint32_t)game.wins[1]);
        centered(119,s,COLOR(255,211,92),2);
        centered(149,game.mode==MATCH_OVER?"TAP TO REMATCH":"TAP FOR NEXT ROUND",white,1);
    }else if(game.ready_ms){
        panel(102,81,116,44,cyan);centered(94,game.ready_ms>350?"READY":"FIGHT!",white,3);
    }
}
static int32_t render(void)
{
    memcpy(canvas,art_arena,sizeof(canvas));
    if(game.mode==MENU){sprite(0,IDLE,78,184,1);sprite(1,IDLE,242,184,-1);}
    else{
        fighter(0);fighter(1);
        for(int i=0;i<4;i++)if(game.bolts[i].life>0){
            Bolt *b=&game.bolts[i];int x=(int)b->x,y=183-(int)b->y;
            rectangle(x-b->dir*12-3,y-2,16,5,b->owner?pink:cyan);
            aster_gfx_disk(&gfx,x,y,7,b->owner?pink:cyan);aster_gfx_disk(&gfx,x+b->dir*2,y,4,white);
        }
        if(game.impact_ms>0){
            int x=game.impact_x,y=game.impact_y,r=7+(180-game.impact_ms)/14;
            uint16_t c=game.impact_guard?cyan:COLOR(255,211,92);
            aster_gfx_line(&gfx,x-r,y-r,x+r,y+r,c);aster_gfx_line(&gfx,x-r,y+r,x+r,y-r,c);
            aster_gfx_line(&gfx,x-r-3,y,x+r+3,y,white);aster_gfx_line(&gfx,x,y-r-3,x,y+r+3,white);
        }
        if(game.flash_ms>0){rectangle(0,180,320,2,COLOR(255,211,92));}
        hud();controls();
    }
    overlay();
    aster_surface_buffer_t b={.size=sizeof(b),.version=1};
    int32_t rc=aster_surface_acquire(surface,&b,sizeof(b));
    if(rc<0)return rc;if(!b.ptr||b.format!=ASTER_PIXEL_RGB565)return ASTER_ENOTSUP;
    if(width==320&&height==240){
        if(b.pitch==640)memcpy(b.ptr,canvas,sizeof(canvas));
        else for(uint32_t y=0;y<240;y++)memcpy((uint8_t*)b.ptr+y*b.pitch,canvas+y*320,640);
        return aster_surface_present(surface);
    }
    /* Keep a 4:3 logical arena, letterbox other runtime dimensions. */
    uint32_t vw=width,vh=width*240/320;
    if(vh>height){vh=height;vw=height*320/240;}
    uint32_t ox=(width-vw)/2,oy=(height-vh)/2;
    for(uint32_t y=0;y<height;y++){
        uint16_t *row=(uint16_t*)((uint8_t*)b.ptr+y*b.pitch);
        if(y<oy||y>=oy+vh){memset(row,0,width*2);continue;}
        uint32_t sy=(y-oy)*240/vh;
        for(uint32_t x=0;x<width;x++)row[x]=(x<ox||x>=ox+vw)?0:canvas[sy*320+(x-ox)*320/vw];
    }
    return aster_surface_present(surface);
}
static void clear_input(void){memset(slots,0,sizeof(slots));keys=0;game.held=0;}
static void update_held(void){game.held=keys;for(int i=0;i<8;i++)game.held|=slots[i];}
static uint32_t hit_button(int x,int y)
{
    if(y<193||y>=240)return 0;
    for(int i=0;i<7;i++)if(x>=button_x[i]&&x<button_x[i]+button_w[i])return 1u<<i;
    return 0;
}
static uint32_t key_button(uint32_t k)
{
    switch(k){case 105:case 30:return BTN_LEFT;case 106:case 32:return BTN_RIGHT;
    case 103:case 17:case 57:return BTN_JUMP;case 108:case 31:return BTN_GUARD;
    case 44:case 36:return BTN_PUNCH;case 45:case 37:return BTN_KICK;case 46:case 38:return BTN_SPECIAL;default:return 0;}
}
static int32_t setup_display(void)
{
    aster_display_info_t d={.size=sizeof(d),.version=1};int32_t rc=aster_display_get_info(0,&d,sizeof(d));if(rc<0)return rc;
    width=d.app_width;height=d.app_height;if(width<160||height<120)return ASTER_ENOTSUP;
    if(surface){aster_surface_destroy(surface);surface=0;}
    aster_surface_desc_t desc={.size=sizeof(desc),.version=1,.width=width,.height=height,.format=ASTER_PIXEL_RGB565};
    return aster_surface_create(&desc,sizeof(desc),&surface);
}
static void release(void)
{
    if(surface){aster_surface_destroy(surface);surface=0;}
    for(int i=0;i<4;i++)if(sounds[i]>0){aster_audio_effect_unload((uint32_t)sounds[i]);sounds[i]=0;}
}
static void play_effect(int i)
{
    if(i<0||i>=4||sounds[i]<=0)return;
    int32_t rc=aster_audio_effect_play((uint32_t)sounds[i]);
    if(rc<0)aster_logf(ASTER_LOG_WARN,"audio play %d failed: %d",i,rc);
}
ASTER_APP_EXPORT int aster_app_init(void)
{
    int32_t rc=setup_display();if(rc<0)return rc;
    aster_gfx_init(&gfx,canvas,320,240,640);game_init(&game);clear_input();previous=aster_time_now_ms();
    if(aster_system_has_feature(ASTER_FEATURE_AUDIO_OUTPUT)==1){
        aster_audio_info_t info={.size=sizeof(info),.version=1};
        int32_t audio_rc=aster_audio_get_info(&info,sizeof(info));
        aster_printf("audio info rc=%d rate=%u channels=%u bits=%u volume=%d",audio_rc,info.sample_rate,info.channels,info.bits,aster_audio_get_volume());
        const char *names[]={"hit","swing","special","bell"};
        const char *directory=info.sample_rate==16000?"/assets/assets/16000":"/assets/assets";
        int loaded=0;
        for(int i=0;i<4&&audio_rc==ASTER_OK&&info.channels==2&&info.bits==16;i++){
            char path[80];aster_snprintf(path,sizeof(path),"%s/%s.wav",directory,names[i]);
            sounds[i]=aster_audio_effect_load(path,(uint32_t)strlen(path));
            if(sounds[i]>0)loaded++;
            else aster_logf(ASTER_LOG_WARN,"effect %d unavailable: %d",i,sounds[i]);
        }
        aster_printf("audio effects loaded: %d/4",loaded);
    }else aster_logf(ASTER_LOG_WARN,"audio output unavailable");
    rc=aster_app_set_framerate(30);if(rc<0){release();return rc;}
    aster_printf("Neon Duel ready: %ux%u, logical 320x240, ABI 4.0",width,height);return ASTER_OK;
}
ASTER_APP_EXPORT void aster_app_start(void){play_effect(3);}
ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
    if(!ev||!surface)return;
    if(ev->type==ASTER_EVENT_FRAME){
        uint32_t now=aster_time_now_ms(),dt=now-previous;previous=now;
        if(dt>165)dt=165;if(!suspended){accumulator+=dt;while(accumulator>=33){game_step(&game,33);accumulator-=33;}}
        frames++;aster_gfx_fonts_pending();
        if(game.effect>0){play_effect(game.effect-1);game.effect=0;}
        int32_t rc=render();if(rc<0){aster_errfmt("render failed %d",rc);aster_app_exit(rc);return;}
    }else if(ev->type==ASTER_EVENT_TOUCH){
        uint32_t slot=ev->u.touch.slot;if(slot>=8)return;
        uint32_t vw=width,vh=width*240/320;if(vh>height){vh=height;vw=height*320/240;}
        int x=((int)ev->u.touch.x-(int)(width-vw)/2)*320/(int)vw;
        int y=((int)ev->u.touch.y-(int)(height-vh)/2)*240/(int)vh;
        if(ev->u.touch.action==ASTER_TOUCH_UP){slots[slot]=0;update_held();return;}
        if(x<0||x>=320||y<0||y>=240){slots[slot]=0;update_held();return;}
        int down=ev->u.touch.action==ASTER_TOUCH_DOWN;
        if(down&&game.mode==MENU){game_start(&game,x>=160&&y>=120&&y<165);clear_input();return;}
        if(down&&game.mode==PAUSED){game_pause(&game);clear_input();return;}
        if(down&&(game.mode==ROUND_OVER||game.mode==MATCH_OVER)){game_press(&game,BTN_PUNCH);clear_input();return;}
        if(down&&x>=139&&x<=181&&y<=49){game_pause(&game);clear_input();return;}
        uint32_t old=slots[slot];slots[slot]=hit_button(x,y);update_held();
        if(slots[slot]&&slots[slot]!=old)game_press(&game,slots[slot]);
    }else if(ev->type==ASTER_EVENT_KEY){
        uint32_t code=ev->u.key.code;
        if(ev->u.key.pressed&&code==1){if(game.mode==MENU){aster_app_exit(0);return;}game_init(&game);clear_input();return;}
        if(ev->u.key.pressed&&(code==25||(code==28&&(game.mode==FIGHT||game.mode==PAUSED)))){game_pause(&game);clear_input();return;}
        uint32_t button=key_button(code),old=keys;
        if(ev->u.key.pressed)keys|=button;else keys&=~button;update_held();
        if(ev->u.key.pressed&&game.mode==MENU&&(button||code==28)){game_start(&game,0);clear_input();}
        else if(ev->u.key.pressed&&!(old&button)&&button)game_press(&game,button);
    }else if(ev->type==ASTER_EVENT_SYS&&ev->u.sys.code==ASTER_SYS_ORIENT){
        clear_input();int32_t rc=setup_display();if(rc<0){aster_app_exit(rc);return;}
    }
}
ASTER_APP_EXPORT void aster_app_pause(void){suspended=1;clear_input();}
ASTER_APP_EXPORT void aster_app_resume(void){suspended=0;clear_input();previous=aster_time_now_ms();accumulator=0;}
ASTER_APP_EXPORT void aster_app_stop(void){release();}
ASTER_APP_EXPORT void aster_app_destroy(void){release();}
