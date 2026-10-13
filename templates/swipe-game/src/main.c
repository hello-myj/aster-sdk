/* Prism Drop — an independent APP using the released Aster SDK ABI 3.4. */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#include "game.h"
#define RGB ASTER_RGB565
#define BG RGB(235,237,244)
#define INK RGB(61,68,83)
#define MUTED RGB(135,145,162)
#define WELL RGB(229,232,240)
#define GRID RGB(221,225,234)
#define WHITE RGB(255,255,255)
static const uint16_t face[7]={RGB(29,176,200),RGB(252,181,78),RGB(153,94,221),RGB(246,144,63),RGB(67,126,227),RGB(49,174,134),RGB(232,100,119)};
static const uint16_t light[7]={RGB(116,226,237),RGB(255,221,145),RGB(207,170,247),RGB(255,202,131),RGB(146,192,255),RGB(132,226,180),RGB(255,167,176)};
static const uint16_t dark[7]={RGB(19,121,153),RGB(194,126,49),RGB(100,60,163),RGB(189,96,38),RGB(45,82,171),RGB(25,116,89),RGB(165,65,87)};
/* Compact pixel labels avoid assuming a 12px system font: SDK has one 20px face. */
static const uint8_t font[36][5]={
 {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
 {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
 {127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
 {127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
 {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{3,4,120,4,3},{97,81,73,69,67}
};
static game_t game;
static aster_handle_t surface=ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[4096];
static uint8_t arena[128];
static int width,height,cell,bx,by;
/* 0 title, 1 play, 2 game over, 3 pause */
static int mode,dirty=1,muted;
static int32_t effects[4];
static uint32_t last_time,fall_time,lock_time,flash_until,last_cue,last_redraw;
static int lock_resets;
static int dragging,down_x,down_y,origin_x;
static uint32_t gesture_serial;

static int absolute(int v){return v<0?-v:v;}
static void label(int x,int y,int scale,uint16_t color,const char *s)
{
    for(;*s;s++,x+=6*scale) {
        int index=*s>='0'&&*s<='9'?*s-'0':*s>='A'&&*s<='Z'?*s-'A'+10:-1;
        if(index<0)continue;
        for(int row=0;row<7;row++) {
            int start=-1;
            for(int col=0;col<=5;col++) {
                int on=col<5&&(font[index][col]&(1<<row));
                if(on&&start<0)start=col;
                if(!on&&start>=0){aster_ge_rect(&ge,x+start*scale,y+row*scale,(col-start)*scale,scale,color);start=-1;}
            }
        }
    }
}
static void centred(int y,int scale,uint16_t color,const char *s)
{label((width-((int)strlen(s)*6-1)*scale)/2,y,scale,color,s);}
static void cue(int index)
{
    uint32_t now=aster_time_now_ms();
    if(muted||effects[index]<=0||(index==0&&now-last_cue<80))return;
    (void)aster_audio_effect_play((uint32_t)effects[index]);last_cue=now;
}
static void tile(int x,int y,int size,int kind)
{
    int k=kind-1;
    aster_ge_rect(&ge,x+3,y+4,size-1,size-1,RGB(186,191,204));
    aster_ge_rect(&ge,x,y,size-1,size-1,dark[k]);
    aster_ge_rect(&ge,x+1,y+1,size-3,size-3,face[k]);
    aster_ge_rect(&ge,x+1,y+1,size-3,2,light[k]);
    aster_ge_rect(&ge,x+1,y+2,1,size-4,light[k]);
    aster_ge_rect(&ge,x+2,y+size-3,size-4,1,dark[k]);
}
static void outline(int x,int y,int s,uint16_t color)
{
    aster_ge_rect(&ge,x,y,s-1,1,color);aster_ge_rect(&ge,x,y+s-2,s-1,1,color);
    aster_ge_rect(&ge,x,y,1,s-1,color);aster_ge_rect(&ge,x+s-2,y,1,s-1,color);
}
static void new_game(void)
{
    game_reset(&game,aster_time_now_ms()^0x7a52b9u);
    mode=1;dirty=1;fall_time=lock_time=0;lock_resets=0;dragging=0;
    flash_until=0;last_time=aster_time_now_ms();cue(0);
}
static void after_lock(int removed)
{
    fall_time=lock_time=0;lock_resets=0;dirty=1;
    if(game.over||removed<0){mode=2;cue(3);aster_printf("Prism Drop: game over score=%u lines=%u\n",game.score,game.lines);}
    else if(removed){flash_until=aster_time_now_ms()+280;cue(2);}
    else cue(1);
}
static void move_horizontal(int dx)
{
    if(game_move(&game,dx,0)) {
        dirty=1;cue(0);
        if(lock_resets<8){lock_time=0;++lock_resets;}
    }
}
static void shift_to_touch(int x)
{
    if(gesture_serial!=game.serial)return;
    int target=origin_x+(x-down_x)/cell;
    if(target< -3)target=-3;if(target>COLS-1)target=COLS-1;
    while(game.x!=target) {
        int dx=target>game.x?1:-1;
        int before=game.x;move_horizontal(dx);if(game.x==before)break;
    }
}
static void render(void)
{
    char text[32];
    aster_ge_clear(&ge,BG);
    centred(10,2,INK,"PRISM DROP");
    /* Sound and pause are separated from the drag area. */
    aster_ge_rect(&ge,9,11,5,8,muted?MUTED:face[0]);
    aster_ge_tri(&ge,14,11,20,7,20,23,muted?MUTED:face[0]);
    if(muted)aster_ge_line_aa(&ge,8,8,23,24,INK);
    aster_ge_rect(&ge,width-22,9,3,13,INK);aster_ge_rect(&ge,width-16,9,3,13,INK);
    aster_snprintf(text,sizeof(text),"SCORE %u",game.score);label(16,33,1,INK,text);
    aster_snprintf(text,sizeof(text),"LINES %u",game.lines);label(width-78,33,1,INK,text);
    /* Raised silver tray, with a recessed pale well. */
    aster_ge_rect(&ge,bx-7+5,by-8+6,COLS*cell+14,ROWS*cell+16,RGB(198,203,215));
    aster_ge_rect(&ge,bx-8,by-9,COLS*cell+16,ROWS*cell+18,RGB(195,201,212));
    aster_ge_rect(&ge,bx-6,by-7,COLS*cell+12,ROWS*cell+14,WHITE);
    aster_ge_rect(&ge,bx-4,by-5,COLS*cell+8,ROWS*cell+10,RGB(157,165,181));
    aster_ge_rect(&ge,bx-1,by-1,COLS*cell+2,ROWS*cell+2,WELL);
    for(int x=1;x<COLS;x++)aster_ge_rect(&ge,bx+x*cell,by,1,ROWS*cell,GRID);
    for(int y=1;y<ROWS;y++)aster_ge_rect(&ge,bx,by+y*cell,COLS*cell,1,GRID);
    for(int y=0;y<ROWS;y++)for(int x=0;x<COLS;x++) {
        int k=game.board[y][x];
        /* The title tableau echoes the supplied image; real games start empty. */
        if(mode==0) {
            if(x==0&&y>=ROWS-6)k=y<ROWS-2?1:4;
            if(y==ROWS-1&&x>=3&&x<=8)k=(x==3?6:x==8?3:x<6?7:5);
            if(y==ROWS-2&&x>=4&&x<=6)k=x==6?5:7;
            if(y==ROWS-3&&x>=6&&x<=8)k=5;
        }
        if(k)tile(bx+x*cell,by+y*cell,cell,k);
    }
    if(mode!=0) {
        int cells[4][2];game_cells(game.kind,game.rotation,cells);
        if(mode==1) {
            int ghost=game_ghost(&game);
            for(int i=0;i<4;i++){int y=ghost+cells[i][1];if(y>=0)outline(bx+(game.x+cells[i][0])*cell,by+y*cell,cell,RGB(164,178,201));}
        }
        if(mode!=2)for(int i=0;i<4;i++) {
            int y=game.y+cells[i][1];
            if(y>=0)tile(bx+(game.x+cells[i][0])*cell,by+y*cell,cell,game.kind+1);
        }
        int nx=bx+COLS*cell+11;label(nx,by+3,1,MUTED,"NEXT");
        game_cells(game.next,0,cells);
        for(int i=0;i<4;i++)tile(nx+cells[i][0]*6,by+19+cells[i][1]*6,6,game.next+1);
    }
    if(flash_until&&aster_time_now_ms()<flash_until) {
        aster_ge_rect(&ge,bx,by-4,COLS*cell,3,face[0]);
        aster_ge_rect(&ge,bx,by+ROWS*cell+3,COLS*cell,3,face[0]);
    }
    centred(height-23,1,MUTED,"SLIDE MOVE   TAP TURN");
    centred(height-12,1,MUTED,"SWIPE DOWN TO DROP");
    if(mode!=1) {
        int top=height/2-48;
        aster_ge_rect_r(&ge,16,top,width-32,96,WHITE,8,0);
        centred(top+12,2,INK,mode==0?"PRISM DROP":mode==2?"GAME OVER":"PAUSED");
        centred(top+40,1,MUTED,mode==0?"COLOUR THE SILVER TRAY":mode==2?"ONE MORE ROUND":"TAKE YOUR TIME");
        centred(top+63,2,face[0],mode==2?"TAP RETRY":mode==3?"TAP RESUME":"TAP PLAY");
    }
    int32_t result=aster_ge_present(&ge);
    dirty=result<0;
    if(result<0) aster_logf(ASTER_LOG_WARN,"frame submit failed: %d",(int)result);
    last_redraw=aster_time_now_ms();
}
ASTER_APP_EXPORT int aster_app_init(void)
{
    aster_surface_desc_t desc={.size=sizeof(desc),.version=1,.format=ASTER_PIXEL_NATIVE,.flags=ASTER_SURFACE_F_PANEL_CANVAS};
    aster_surface_buffer_t buf={.size=sizeof(buf),.version=1};
    int err=aster_surface_create(&desc,sizeof(desc),&surface);if(err!=ASTER_OK)return err;
    err=aster_surface_acquire(surface,&buf,sizeof(buf));
    if(err!=ASTER_OK){aster_surface_destroy(surface);surface=ASTER_HANDLE_INVALID;return err;}
    width=(int)buf.width;height=(int)buf.height;
    cell=(height-90)/ROWS;if(cell>(width-84)/COLS)cell=(width-84)/COLS;
    if(cell<8){aster_surface_destroy(surface);surface=ASTER_HANDLE_INVALID;return ASTER_EINVAL;}
    bx=(width-COLS*cell)/2;by=52;
    aster_ge_init(&ge,surface,buf.width,buf.height,ring,4096,arena,sizeof(arena));
    aster_app_set_framerate(30);
    game_reset(&game,1);mode=0;dirty=1;last_time=aster_time_now_ms();
    const char *paths[4]={"/assets/audio/move.wav","/assets/audio/drop.wav","/assets/audio/clear.wav","/assets/audio/over.wav"};
    int loaded=0;
    for(int i=0;i<4;i++){effects[i]=aster_audio_effect_load(paths[i],(uint32_t)strlen(paths[i]));if(effects[i]>0)++loaded;}
    aster_printf("Prism Drop: effects loaded %d/4, surface %dx%d\n",loaded,width,height);
    /* Let the first frame draw after the launcher switches to the APP. */
    return ASTER_OK;
}
ASTER_APP_EXPORT void aster_app_stop(void)
{
    for(int i=0;i<4;i++)if(effects[i]>0){aster_audio_effect_unload((uint32_t)effects[i]);effects[i]=0;}
    if(surface!=ASTER_HANDLE_INVALID){aster_surface_destroy(surface);surface=ASTER_HANDLE_INVALID;}
}
ASTER_APP_EXPORT void aster_app_pause(void){if(mode==1)mode=3;dragging=0;dirty=1;}
ASTER_APP_EXPORT void aster_app_resume(void){last_time=aster_time_now_ms();dirty=1;}
ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
    if(ev->type==ASTER_EVENT_TOUCH) {
        int x=(int)ev->u.touch.x,y=(int)ev->u.touch.y;
        if(ev->u.touch.action==ASTER_TOUCH_DOWN) {
            dragging=0;
            if(y<28&&x<30){muted=!muted;dirty=1;return;}
            if(y<28&&x>width-30){if(mode==1)mode=3;else if(mode==3){mode=1;last_time=aster_time_now_ms();}dirty=1;return;}
            if(mode==0||mode==2){new_game();return;}
            if(mode==3){mode=1;last_time=aster_time_now_ms();dirty=1;return;}
            if(y>=by-8&&y<height-30){dragging=1;down_x=x;down_y=y;origin_x=game.x;gesture_serial=game.serial;}
        } else if(ev->u.touch.action==ASTER_TOUCH_MOVE&&dragging&&mode==1) {
            shift_to_touch(x);
        } else if(ev->u.touch.action==ASTER_TOUCH_UP&&dragging) {
            dragging=0;
            if(mode!=1||gesture_serial!=game.serial)return;
            int dx=x-down_x,dy=y-down_y;
            if(dy>28&&dy>absolute(dx)){after_lock(game_drop(&game));}
            else if(absolute(dx)<8&&absolute(dy)<8) {
                if(game_rotate(&game)){dirty=1;cue(0);if(lock_resets<8){lock_time=0;++lock_resets;}}
            } else shift_to_touch(x);
        }
    } else if(ev->type==ASTER_EVENT_FRAME) {
        uint32_t now=aster_time_now_ms(),dt=now-last_time;last_time=now;if(dt>250)dt=250;
        if(mode==1) {
            uint32_t interval=game.lines>=30?110:650-game.lines*18;
            fall_time+=dt;
            if(fall_time>=interval){fall_time=0;if(game_move(&game,0,1)){dirty=1;lock_time=0;}}
            if(!game_fits(&game,game.x,game.y+1,game.rotation)) {
                lock_time+=dt;if(lock_time>=350)after_lock(game_lock(&game));
            } else lock_time=0;
        }
        if(flash_until&&now>=flash_until){flash_until=0;dirty=1;}
        if(dirty || now-last_redraw>=200)render();
    }
}
