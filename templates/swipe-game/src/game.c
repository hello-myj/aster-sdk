#include "game.h"
/* I, O, T, L, J, S, Z. Rotation pivots are the centres of 4x4 / 3x3 boxes. */
static const uint8_t shape[7][4][2] = {
    {{0,1},{1,1},{2,1},{3,1}}, {{1,0},{2,0},{1,1},{2,1}},
    {{1,0},{0,1},{1,1},{2,1}}, {{2,0},{0,1},{1,1},{2,1}},
    {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{0,1},{1,1}},
    {{0,0},{1,0},{1,1},{2,1}}
};
void game_cells(int kind, int rotation, int out[4][2])
{
    for (int i=0;i<4;i++) {
        int x=shape[kind][i][0], y=shape[kind][i][1];
        if (kind!=1) for (int r=0;r<(rotation&3);r++) {
            int old=x; x=(kind==0?3:2)-y; y=old;
        }
        out[i][0]=x; out[i][1]=y;
    }
}
static uint32_t random_next(game_t *g)
{ g->rng=g->rng*1664525u+1013904223u; return g->rng; }
static int draw_bag(game_t *g)
{
    if (g->bag_pos>=7) {
        for (int i=0;i<7;i++) g->bag[i]=(uint8_t)i;
        for (int i=6;i>0;i--) {
            int j=(int)(random_next(g)%(uint32_t)(i+1));
            uint8_t temp=g->bag[i];g->bag[i]=g->bag[j];g->bag[j]=temp;
        }
        g->bag_pos=0;
    }
    return g->bag[g->bag_pos++];
}
int game_fits(const game_t *g,int x,int y,int rotation)
{
    int cells[4][2];game_cells(g->kind,rotation,cells);
    for(int i=0;i<4;i++) {
        int cx=x+cells[i][0],cy=y+cells[i][1];
        if(cx<0||cx>=COLS||cy>=ROWS||cy< -4) return 0;
        if(cy>=0&&g->board[cy][cx]) return 0;
    }
    return 1;
}
static void spawn(game_t *g)
{
    g->kind=g->next;g->next=draw_bag(g);g->rotation=0;g->x=3;g->y=-1;
    ++g->serial;
    if(!game_fits(g,g->x,g->y,0)) g->over=1;
}
void game_reset(game_t *g,uint32_t seed)
{
    for(int y=0;y<ROWS;y++)for(int x=0;x<COLS;x++)g->board[y][x]=0;
    g->rng=seed?seed:1;g->bag_pos=7;g->over=0;g->score=0;g->lines=0;g->serial=0;
    g->next=draw_bag(g);spawn(g);
}
int game_move(game_t *g,int dx,int dy)
{
    if(g->over||!game_fits(g,g->x+dx,g->y+dy,g->rotation))return 0;
    g->x+=dx;g->y+=dy;return 1;
}
int game_rotate(game_t *g)
{
    if(g->over||g->kind==1)return 0;
    const int kicks[6][2]={{0,0},{-1,0},{1,0},{-2,0},{2,0},{0,-1}};
    int r=(g->rotation+1)&3;
    for(int i=0;i<6;i++)if(game_fits(g,g->x+kicks[i][0],g->y+kicks[i][1],r)) {
        g->x+=kicks[i][0];g->y+=kicks[i][1];g->rotation=r;return 1;
    }
    return 0;
}
int game_ghost(const game_t *g)
{int y=g->y;while(game_fits(g,g->x,y+1,g->rotation))++y;return y;}
int game_lock(game_t *g)
{
    if(g->over)return -1;
    int cells[4][2];game_cells(g->kind,g->rotation,cells);
    for(int i=0;i<4;i++)if(g->y+cells[i][1]<0){g->over=1;return -1;}
    for(int i=0;i<4;i++)g->board[g->y+cells[i][1]][g->x+cells[i][0]]=(uint8_t)(g->kind+1);
    int count=0,write=ROWS-1;
    for(int y=ROWS-1;y>=0;y--) {
        int full=1;for(int x=0;x<COLS;x++)if(!g->board[y][x])full=0;
        if(full){++count;continue;}
        for(int x=0;x<COLS;x++)g->board[write][x]=g->board[y][x];
        --write;
    }
    while(write>=0){for(int x=0;x<COLS;x++)g->board[write][x]=0;--write;}
    const uint32_t points[5]={0,100,300,500,800};
    g->score+=points[count]*(1+g->lines/10);g->lines+=(uint32_t)count;
    spawn(g);return count;
}
int game_drop(game_t *g)
{
    if(g->over)return -1;
    int y=game_ghost(g);g->score+=(uint32_t)(y-g->y)*2;g->y=y;return game_lock(g);
}
