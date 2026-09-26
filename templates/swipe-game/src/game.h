#ifndef PRISM_GAME_H
#define PRISM_GAME_H
#include <stdint.h>
#define COLS 10
#define ROWS 16
typedef struct {
    uint8_t board[ROWS][COLS], bag[7];
    int bag_pos, kind, next, rotation, x, y, over;
    uint32_t rng, score, lines, serial;
} game_t;
void game_cells(int kind, int rotation, int out[4][2]);
void game_reset(game_t *g, uint32_t seed);
int game_fits(const game_t *g, int x, int y, int rotation);
int game_move(game_t *g, int dx, int dy);
int game_rotate(game_t *g);
int game_ghost(const game_t *g);
/* Lock/drop return removed row count, or -1 on top-out. */
int game_lock(game_t *g);
int game_drop(game_t *g);
#endif
