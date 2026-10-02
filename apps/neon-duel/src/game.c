#include "game.h"
#ifdef NEON_HOST_TEST
#include <string.h>
#else
#include <aster_imports.h>
#endif
static float absolute(float n) { return n<0?-n:n; }
static uint32_t random_next(Game *g) { g->rng=g->rng*1664525u+1013904223u;return g->rng; }
static void round_start(Game *g)
{
    memset(g->p,0,sizeof(g->p));memset(g->bolts,0,sizeof(g->bolts));
    for(int i=0;i<2;i++){g->p[i].hp=100;g->p[i].energy=25;g->p[i].x=i?238:82;g->p[i].facing=i?-1:1;}
    g->held=0;g->mode=FIGHT;g->round_ms=90000;g->ready_ms=1100;
    g->ai_ms=450;g->regen_ms=0;g->phase_ms=0;g->hitstop_ms=0;g->impact_ms=0;g->effect=4;
}
void game_init(Game *g){ memset(g,0,sizeof(*g));g->rng=77;g->mode=MENU;g->round=1;g->winner=2; }
void game_start(Game *g,int training){ game_init(g);g->training=training;round_start(g); }
static void attack(Game *g,int who,int move)
{
    Fighter *p=&g->p[who];
    if(p->attack||p->stun_ms||p->hp<=0||p->guarding)return;
    if(move==SPECIAL&&p->energy<60)return;
    if(move==SPECIAL)p->energy-=60;
    p->attack=move;p->attack_ms=0;p->landed=0;p->attack_dir=p->facing;
    g->effect=move==SPECIAL?3:2;
}
void game_press(Game *g,uint32_t button)
{
    if(g->mode==MENU){game_start(g,0);return;}
    if(g->mode==ROUND_OVER||g->mode==MATCH_OVER){
        if(g->phase_ms<700)return;
        if(g->mode==MATCH_OVER){game_start(g,g->training);return;}
        g->round++;round_start(g);return;
    }
    if(g->mode!=FIGHT||g->ready_ms)return;
    Fighter *p=&g->p[0];
    if(button==BTN_JUMP&&!p->y&&!p->stun_ms&&!p->attack){
        p->vy=225;p->y=.1f;
        p->drift=(g->held&BTN_LEFT)?-90:(g->held&BTN_RIGHT)?90:p->facing*64;
        p->guarding=0;
    }
    if(button==BTN_PUNCH)attack(g,0,PUNCH);
    if(button==BTN_KICK)attack(g,0,KICK);
    if(button==BTN_SPECIAL)attack(g,0,SPECIAL);
}
void game_pause(Game *g){ if(g->mode==FIGHT){g->mode=PAUSED;g->held=0;}else if(g->mode==PAUSED){g->mode=FIGHT;g->held=0;} }
static void strike(Game *g,int owner,int damage,int dir,int ranged)
{
    Fighter *p=&g->p[owner],*q=&g->p[1-owner];
    int blocked=q->guarding&&!q->y&&q->facing==-dir;
    if(blocked){damage=damage/5;g->blocked++;}
    q->hp-=damage;if(q->hp<0)q->hp=0;
    q->stun_ms=blocked?90:220;q->attack=0;q->guarding=0;
    q->x+=dir*(blocked?3:ranged?14:8);
    p->energy+=blocked?4:12;if(p->energy>100)p->energy=100;
    q->energy+=7;if(q->energy>100)q->energy=100;
    if(!blocked){p->combo=p->combo_ms>0?p->combo+1:1;p->combo_ms=1000;g->hits++;}
    g->impact_x=(int)q->x;g->impact_y=148-(int)q->y;g->impact_ms=180;g->impact_guard=blocked;
    g->hitstop_ms=blocked?30:60;g->flash_ms=ranged&&!blocked?90:0;g->effect=1;
}
static void finish(Game *g)
{
    int a=g->p[0].hp,b=g->p[1].hp;
    if(a>0&&b>0&&g->round_ms>0)return;
    g->winner=a>b?0:a<b?1:2;
    if(g->training){
        g->p[0].hp=100;g->p[1].hp=100;g->p[0].energy=100;g->round_ms=90000;return;
    }
    if(g->winner<2)g->wins[g->winner]++;
    g->mode=(g->wins[0]>=2||g->wins[1]>=2)?MATCH_OVER:ROUND_OVER;
    g->phase_ms=0;g->held=0;g->effect=4;
}
void game_step(Game *g,int ms)
{
    if(g->mode==MENU||g->mode==PAUSED)return;
    if(g->mode==ROUND_OVER||g->mode==MATCH_OVER){g->phase_ms+=ms;return;}
    if(g->ready_ms){g->ready_ms-=ms;if(g->ready_ms<0)g->ready_ms=0;return;}
    if(g->impact_ms>0)g->impact_ms-=ms;
    if(g->flash_ms>0)g->flash_ms-=ms;
    if(g->hitstop_ms>0){g->hitstop_ms-=ms;return;}
    g->round_ms-=ms;if(g->round_ms<0)g->round_ms=0;
    g->regen_ms+=ms;
    if(g->regen_ms>=240){g->regen_ms-=240;for(int i=0;i<2;i++)if(g->p[i].energy<100)g->p[i].energy++;}
    Fighter *a=&g->p[0],*b=&g->p[1];
    for(int i=0;i<2;i++){
        Fighter *p=&g->p[i],*q=&g->p[1-i];
        if(!p->attack&&!p->stun_ms)p->facing=q->x>=p->x?1:-1;
        if(p->stun_ms>0){p->stun_ms-=ms;if(p->stun_ms<0)p->stun_ms=0;}
        if(p->combo_ms>0)p->combo_ms-=ms;
        p->moving=0;
    }
    a->guarding=(g->held&BTN_GUARD)&&!a->y&&!a->attack&&!a->stun_ms;
    b->guarding=0;
    if(!a->stun_ms&&!a->attack&&!a->guarding&&!a->y){
        int dir=((g->held&BTN_RIGHT)?1:0)-((g->held&BTN_LEFT)?1:0);
        a->x+=dir*78*ms/1000.f;a->moving=dir!=0;
    }
    float distance=absolute(a->x-b->x);
    if(!g->training){
        g->ai_ms-=ms;
        if(!b->attack&&!b->stun_ms&&!b->y){
            if(a->attack&&distance<67&&((g->rng>>8)&3)==0)b->guarding=1;
            if(!b->guarding&&distance>43){b->x+=b->facing*49*ms/1000.f;b->moving=1;}
            if(g->ai_ms<=0){
                uint32_t r=random_next(g);g->ai_ms=380+(int)(r%380);
                if(distance<60)attack(g,1,(r&1)?PUNCH:KICK);
                else if(b->energy>=60&&(r%3==0))attack(g,1,SPECIAL);
                else if(distance<100&&(r%4==0)){b->y=.1f;b->vy=220;b->drift=b->facing*60;}
            }
        }
    }
    for(int i=0;i<2;i++){
        Fighter *p=&g->p[i],*q=&g->p[1-i];
        if(p->y>0){p->x+=p->drift*ms/1000.f;p->y+=p->vy*ms/1000.f;p->vy-=680*ms/1000.f;if(p->y<=0){p->y=0;p->vy=0;p->drift=0;}}
        if(p->attack){
            p->attack_ms+=ms;
            int onset=p->attack==PUNCH?100:p->attack==KICK?160:190;
            int duration=p->attack==PUNCH?330:p->attack==KICK?480:600;
            if(p->attack_ms>=onset&&!p->landed){
                if(p->attack==SPECIAL){
                    for(int j=0;j<4;j++)if(!g->bolts[j].life){g->bolts[j]=(Bolt){p->x+p->attack_dir*26,p->y+29,i,p->attack_dir,1400};break;}
                    p->landed=1;
                }else{
                    float gap=(q->x-p->x)*p->attack_dir;
                    int reach=p->attack==PUNCH?47:64;
                    if(gap>0&&gap<reach&&absolute(p->y-q->y)<25){strike(g,i,p->attack==PUNCH?9:15,p->attack_dir,0);p->landed=1;}
                    if(p->attack_ms>=onset+90)p->landed=1;
                }
            }
            if(p->attack_ms>=duration)p->attack=0;
        }
    }
    for(int j=0;j<4;j++){
        Bolt *v=&g->bolts[j];if(v->life<=0){v->life=0;continue;}
        v->life-=ms;v->x+=v->dir*180*ms/1000.f;
        Fighter *q=&g->p[1-v->owner];
        if(absolute(v->x-q->x)<19&&absolute(v->y-(q->y+29))<25){strike(g,v->owner,24,v->dir,1);v->life=0;}
        if(v->x<0||v->x>320||v->life<0)v->life=0;
    }
    if(absolute(a->x-b->x)<29&&absolute(a->y-b->y)<30){
        float mid=(a->x+b->x)*.5f;
        if(a->x<=b->x){a->x=mid-14.5f;b->x=mid+14.5f;}else{a->x=mid+14.5f;b->x=mid-14.5f;}
    }
    for(int i=0;i<2;i++){if(g->p[i].x<24)g->p[i].x=24;if(g->p[i].x>296)g->p[i].x=296;}
    finish(g);
}
int game_pose(const Fighter *p,int frame)
{
    if(p->hp<=0)return KO;
    if(p->stun_ms>0)return HURT;
    if(p->attack)return p->attack;
    if(p->guarding)return GUARD;
    if(p->y>0)return JUMP;
    if(p->moving&&(frame/4)%2)return WALK;
    return IDLE;
}
