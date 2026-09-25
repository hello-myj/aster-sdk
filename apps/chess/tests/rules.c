/* Host verification of the same chess.c compiled into the Aster app. */
#include <assert.h>
#include <stdio.h>
#include "chess.h"

static int square(const char *s) { return (int)('8' - s[1]) * 8 + s[0] - 'a'; }

static chess_move_t find_move(const chess_board_t *b, const char *from,
			      const char *to, int promote)
{
	chess_move_t moves[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, moves, CHESS_MAX_MOVES);
	for (int i = 0; i < n; i++) {
		if (moves[i].from == square(from) && moves[i].to == square(to) &&
		    moves[i].promote == promote) return moves[i];
	}
	assert(!"expected legal move");
	return (chess_move_t){ 0, 0, 0 };
}

static void play(chess_board_t *b, const char *from, const char *to)
{
	chess_board_t next;
	chess_apply(&next, b, find_move(b, from, to, 0));
	*b = next;
}

static unsigned long perft(const chess_board_t *b, int depth)
{
	if (!depth) return 1;
	chess_move_t moves[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, moves, CHESS_MAX_MOVES);
	assert(n <= CHESS_MAX_MOVES);
	unsigned long total = 0;
	for (int i = 0; i < n; i++) {
		chess_board_t next;
		chess_apply(&next, b, moves[i]);
		total += perft(&next, depth - 1);
	}
	return total;
}

int main(void)
{
	chess_board_t b;
	chess_reset(&b);
	assert(perft(&b, 1) == 20);
	assert(perft(&b, 2) == 400);
	assert(perft(&b, 3) == 8902);
	assert(perft(&b, 4) == 197281);
	chess_move_t ai = chess_best_move(&b, 7);
	assert(ai.from != ai.to);
	find_move(&b, "e2", "e4", 0);

	/* En passant captures the pawn behind the target and clears the flag. */
	chess_reset(&b);
	play(&b, "e2", "e4"); play(&b, "a7", "a6");
	play(&b, "e4", "e5"); play(&b, "d7", "d5");
	assert(b.ep == square("d6"));
	play(&b, "e5", "d6");
	assert(b.sq[square("d6")] == CHESS_PAWN);
	assert(b.sq[square("d5")] == 0 && b.ep == -1);

	/* Both sides can castle; crossing an attacked square is forbidden. */
	chess_reset(&b);
	for (int i = 0; i < 64; i++) b.sq[i] = 0;
	b.sq[square("e1")] = CHESS_KING;
	b.sq[square("a1")] = b.sq[square("h1")] = CHESS_ROOK;
	b.sq[square("e8")] = -CHESS_KING;
	b.sq[square("a8")] = b.sq[square("h8")] = -CHESS_ROOK;
	find_move(&b, "e1", "g1", 0);
	find_move(&b, "e1", "c1", 0);
	b.sq[square("f8")] = -CHESS_ROOK;
	chess_move_t moves[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&b, moves, CHESS_MAX_MOVES);
	for (int i = 0; i < n; i++)
		assert(!(moves[i].from == square("e1") && moves[i].to == square("g1")));
	b.sq[square("f8")] = 0;
	chess_board_t next;
	chess_apply(&next, &b, find_move(&b, "e1", "g1", 0));
	assert(next.sq[square("g1")] == CHESS_KING);
	assert(next.sq[square("f1")] == CHESS_ROOK);
	assert((next.castle & 3) == 0);

	/* Promotion offers all four choices. */
	chess_reset(&b);
	for (int i = 0; i < 64; i++) b.sq[i] = 0;
	b.sq[square("e1")] = CHESS_KING;
	b.sq[square("h8")] = -CHESS_KING;
	b.sq[square("a7")] = CHESS_PAWN;
	for (int p = CHESS_KNIGHT; p <= CHESS_QUEEN; p++)
		find_move(&b, "a7", "a8", p);

	/* Fool's mate and a textbook stalemate. */
	chess_reset(&b);
	play(&b, "f2", "f3"); play(&b, "e7", "e5");
	play(&b, "g2", "g4"); play(&b, "d8", "h4");
	assert(chess_status(&b) == CHESS_CHECKMATE);
	chess_reset(&b);
	for (int i = 0; i < 64; i++) b.sq[i] = 0;
	b.sq[square("a8")] = -CHESS_KING;
	b.sq[square("c6")] = CHESS_KING;
	b.sq[square("b6")] = CHESS_QUEEN;
	b.side = CHESS_BLACK;
	assert(chess_status(&b) == CHESS_STALEMATE);

	puts("chess rules OK");
	return 0;
}
