#ifndef NEON_GAME_H
#define NEON_GAME_H
#include <stdint.h>
enum { BTN_LEFT=1, BTN_RIGHT=2, BTN_JUMP=4, BTN_GUARD=8, BTN_PUNCH=16, BTN_KICK=32, BTN_SPECIAL=64 };
enum { MENU, FIGHT, ROUND_OVER, MATCH_OVER, PAUSED };
enum { IDLE, WALK, PUNCH, KICK, GUARD, JUMP, HURT, KO, SPECIAL };
typedef struct {
    float x, y, vy, drift;
    int hp, energy, facing, attack_dir, attack, attack_ms, landed, stun_ms;
    int guarding, moving, combo, combo_ms;
} Fighter;
typedef struct { float x,y; int owner,dir,life; } Bolt;
typedef struct {
    Fighter p[2]; Bolt bolts[4];
    int mode, training, round, wins[2], winner, round_ms, ready_ms;
    int phase_ms, ai_ms, regen_ms, hitstop_ms, flash_ms, effect, hits, blocked;
    int impact_x, impact_y, impact_ms, impact_guard;
    uint32_t held, rng;
} Game;
void game_init(Game *g);
void game_start(Game *g, int training);
void game_press(Game *g, uint32_t button);
void game_step(Game *g, int ms);
void game_pause(Game *g);
int game_pose(const Fighter *p, int frame);
#endif
