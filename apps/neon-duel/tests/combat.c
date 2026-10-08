#include <assert.h>
#include <stdio.h>
#include "../src/game.h"
static void step(Game *g,int n){for(int i=0;i<n;i++)game_step(g,33);}
static void setup(Game *g){game_start(g,1);g->ready_ms=0;g->p[0].x=100;g->p[1].x=138;}
int main(void)
{
    Game g;game_init(&g);assert(g.mode==MENU);
    setup(&g);game_press(&g,BTN_PUNCH);step(&g,7);assert(g.p[1].hp==91&&g.hits==1);step(&g,20);assert(g.p[1].hp==91); /* one damage event per attack */
    setup(&g);g.p[1].x=180;game_press(&g,BTN_PUNCH);step(&g,20);assert(g.p[1].hp==100); /* miss */
    setup(&g);g.held=BTN_GUARD;g.p[1].attack=PUNCH;g.p[1].attack_dir=-1;step(&g,7);assert(g.p[0].hp==99&&g.blocked==1); /* chip damage */
    setup(&g);g.p[1].y=40;g.p[1].vy=0;game_press(&g,BTN_PUNCH);step(&g,5);assert(g.p[1].hp==100); /* jump evades */
    setup(&g);game_press(&g,BTN_SPECIAL);assert(g.p[0].attack==0&&g.p[0].energy==25);
    g.p[0].energy=60;game_press(&g,BTN_SPECIAL);assert(g.p[0].energy==0);step(&g,12);assert(g.p[1].hp==76); /* projectile collision */
    setup(&g);g.p[1].x=240;g.held=BTN_RIGHT;step(&g,80);assert(g.p[0].x<=296&&g.p[1].x<=296&&g.p[1].x-g.p[0].x>=28); /* collision/walls */
    setup(&g);game_press(&g,BTN_JUMP);step(&g,35);assert(g.p[0].y==0); /* land */
    setup(&g);game_pause(&g);float x=g.p[0].x;int time=g.round_ms;step(&g,90);assert(g.p[0].x==x&&g.round_ms==time);game_pause(&g);assert(g.mode==FIGHT);
    game_start(&g,0);g.ready_ms=0;g.p[1].hp=0;game_step(&g,33);assert(g.mode==ROUND_OVER&&g.wins[0]==1);
    g.phase_ms=800;game_press(&g,BTN_PUNCH);assert(g.mode==FIGHT&&g.round==2&&g.p[1].hp==100);
    g.ready_ms=0;g.p[1].hp=0;game_step(&g,33);assert(g.mode==MATCH_OVER&&g.wins[0]==2);
    g.phase_ms=800;game_press(&g,BTN_PUNCH);assert(g.mode==FIGHT&&g.wins[0]==0&&g.round==1);
    game_start(&g,0);g.ready_ms=0;g.round_ms=1;g.p[0].hp=70;g.p[1].hp=80;game_step(&g,33);assert(g.winner==1&&g.mode==ROUND_OVER);
    game_start(&g,0);g.ready_ms=0;g.round_ms=1;game_step(&g,33);assert(g.winner==2&&g.wins[0]==0&&g.wins[1]==0);
    setup(&g);g.p[1].hp=0;game_step(&g,33);assert(g.mode==FIGHT&&g.p[1].hp==100); /* training resets */
    game_start(&g,0);step(&g,3000);assert(g.mode==ROUND_OVER&&g.wins[1]==1); /* AI can play to KO */
    puts("PASS: melee, misses, single hit, guard, air dodge, burst, walls, landing, pause, rounds, match, timeout, draw, training, AI");
    return 0;
}
