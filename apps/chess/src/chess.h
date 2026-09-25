/* SPDX-License-Identifier: MIT */
#ifndef ASTER_CHESS_H
#define ASTER_CHESS_H

#include <stdint.h>

enum { CHESS_PAWN = 1, CHESS_KNIGHT, CHESS_BISHOP, CHESS_ROOK,
       CHESS_QUEEN, CHESS_KING };
enum { CHESS_WHITE = 1, CHESS_BLACK = -1 };
enum { CHESS_PLAYING, CHESS_CHECKMATE, CHESS_STALEMATE,
       CHESS_FIFTY_MOVES, CHESS_INSUFFICIENT };

#define CHESS_MAX_MOVES 256

typedef struct {
	int8_t sq[64]; /* positive white, negative black; row 0 is black's back rank */
	int8_t side;
	uint8_t castle; /* white K/Q = 1/2, black K/Q = 4/8 */
	int8_t ep;      /* en passant target, or -1 */
	uint16_t halfmove;
	uint16_t fullmove;
} chess_board_t;

typedef struct {
	uint8_t from, to;
	uint8_t promote; /* 0 or KNIGHT/BISHOP/ROOK/QUEEN */
} chess_move_t;

void chess_reset(chess_board_t *board);
int chess_legal_moves(const chess_board_t *board, chess_move_t *moves, int capacity);
void chess_apply(chess_board_t *dst, const chess_board_t *src, chess_move_t move);
int chess_in_check(const chess_board_t *board, int side);
int chess_status(const chess_board_t *board);
uint32_t chess_key(const chess_board_t *board);
chess_move_t chess_best_move(const chess_board_t *board, uint32_t seed);

#endif
