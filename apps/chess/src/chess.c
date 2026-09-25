/* SPDX-License-Identifier: MIT
 * Small, dependency-free chess rules and two-ply opponent for the Aster app.
 */
#include "chess.h"

static int abs_i(int v) { return v < 0 ? -v : v; }
static int sign_i(int v) { return (v > 0) - (v < 0); }
static int valid(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

void chess_reset(chess_board_t *b)
{
	static const int8_t back[8] = {
		CHESS_ROOK, CHESS_KNIGHT, CHESS_BISHOP, CHESS_QUEEN,
		CHESS_KING, CHESS_BISHOP, CHESS_KNIGHT, CHESS_ROOK
	};
	for (int i = 0; i < 64; i++) b->sq[i] = 0;
	for (int f = 0; f < 8; f++) {
		b->sq[f] = -back[f];
		b->sq[8 + f] = -CHESS_PAWN;
		b->sq[48 + f] = CHESS_PAWN;
		b->sq[56 + f] = back[f];
	}
	b->side = CHESS_WHITE;
	b->castle = 15;
	b->ep = -1;
	b->halfmove = 0;
	b->fullmove = 1;
}

/* All attacks, including pinned pieces, matter for king safety and castling. */
static int attacked(const chess_board_t *b, int target, int by)
{
	int tf = target % 8, tr = target / 8;
	for (int from = 0; from < 64; from++) {
		int piece = b->sq[from];
		if (sign_i(piece) != by) continue;
		int f = from % 8, r = from / 8;
		int df = tf - f, dr = tr - r;
		switch (abs_i(piece)) {
		case CHESS_PAWN:
			if (dr == -by && abs_i(df) == 1) return 1;
			break;
		case CHESS_KNIGHT:
			if ((abs_i(df) == 1 && abs_i(dr) == 2) ||
			    (abs_i(df) == 2 && abs_i(dr) == 1)) return 1;
			break;
		case CHESS_KING:
			if (abs_i(df) <= 1 && abs_i(dr) <= 1) return 1;
			break;
		default: {
			int diagonal = abs_i(df) == abs_i(dr) && df != 0;
			int straight = (df == 0) != (dr == 0);
			int kind = abs_i(piece);
			if (!((kind == CHESS_BISHOP && diagonal) ||
			      (kind == CHESS_ROOK && straight) ||
			      (kind == CHESS_QUEEN && (diagonal || straight)))) break;
			int sf = sign_i(df), sr = sign_i(dr);
			int x = f + sf, y = r + sr;
			while (x != tf || y != tr) {
				if (b->sq[y * 8 + x]) break;
				x += sf;
				y += sr;
			}
			if (x == tf && y == tr) return 1;
			break;
		}
		}
	}
	return 0;
}

int chess_in_check(const chess_board_t *b, int side)
{
	for (int i = 0; i < 64; i++) {
		if (b->sq[i] == side * CHESS_KING) return attacked(b, i, -side);
	}
	return 1;
}

void chess_apply(chess_board_t *dst, const chess_board_t *src, chess_move_t m)
{
	*dst = *src;
	int piece = src->sq[m.from], captured = src->sq[m.to];
	int side = src->side;
	dst->sq[m.from] = 0;
	dst->sq[m.to] = m.promote ? side * m.promote : piece;
	dst->ep = -1;
	dst->halfmove = (abs_i(piece) == CHESS_PAWN || captured) ? 0 :
		(uint16_t)(src->halfmove + 1);
	if (abs_i(piece) == CHESS_PAWN) {
		if (abs_i((int)m.to - (int)m.from) == 16)
			dst->ep = (int8_t)((m.from + m.to) / 2);
		if (m.to == src->ep && !captured) {
			dst->sq[(int)m.to + 8 * side] = 0;
			dst->halfmove = 0;
		}
	}
	if (abs_i(piece) == CHESS_KING) {
		dst->castle &= side == CHESS_WHITE ? (uint8_t)~3u : (uint8_t)~12u;
		if (m.to == m.from + 2) {
			dst->sq[m.from + 1] = dst->sq[m.from + 3];
			dst->sq[m.from + 3] = 0;
		} else if (m.from == m.to + 2) {
			dst->sq[m.from - 1] = dst->sq[m.from - 4];
			dst->sq[m.from - 4] = 0;
		}
	}
	if (m.from == 63 || m.to == 63) dst->castle &= (uint8_t)~1u;
	if (m.from == 56 || m.to == 56) dst->castle &= (uint8_t)~2u;
	if (m.from == 7 || m.to == 7) dst->castle &= (uint8_t)~4u;
	if (m.from == 0 || m.to == 0) dst->castle &= (uint8_t)~8u;
	if (side == CHESS_BLACK) dst->fullmove++;
	dst->side = (int8_t)-side;
}

static void emit(const chess_board_t *b, chess_move_t *moves, int cap, int *count,
		 int from, int to, int promote)
{
	if (to < 0 || to >= 64 || abs_i(b->sq[to]) == CHESS_KING) return;
	chess_move_t m = { (uint8_t)from, (uint8_t)to, (uint8_t)promote };
	chess_board_t next;
	chess_apply(&next, b, m);
	if (chess_in_check(&next, b->side)) return;
	if (*count < cap) moves[*count] = m;
	(*count)++;
}

static void pawn_move(const chess_board_t *b, chess_move_t *moves, int cap,
		      int *count, int from, int to)
{
	if (to / 8 == (b->side == CHESS_WHITE ? 0 : 7)) {
		for (int p = CHESS_QUEEN; p >= CHESS_KNIGHT; p--)
			emit(b, moves, cap, count, from, to, p);
	} else {
		emit(b, moves, cap, count, from, to, 0);
	}
}

int chess_legal_moves(const chess_board_t *b, chess_move_t *moves, int cap)
{
	static const int8_t knight_f[8] = { 1, 2, 2, 1, -1, -2, -2, -1 };
	static const int8_t knight_r[8] = { -2, -1, 1, 2, 2, 1, -1, -2 };
	static const int8_t ray_f[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
	static const int8_t ray_r[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };
	int count = 0, side = b->side;
	for (int from = 0; from < 64; from++) {
		int piece = b->sq[from];
		if (sign_i(piece) != side) continue;
		int kind = abs_i(piece), f = from % 8, r = from / 8;
		if (kind == CHESS_PAWN) {
			int nr = r - side;
			if (!valid(f, nr)) continue;
			int to = nr * 8 + f;
			if (!b->sq[to]) {
				pawn_move(b, moves, cap, &count, from, to);
				int nr2 = r - 2 * side;
				if (r == (side == CHESS_WHITE ? 6 : 1) &&
				    !b->sq[nr2 * 8 + f])
					emit(b, moves, cap, &count, from, nr2 * 8 + f, 0);
			}
			for (int df = -1; df <= 1; df += 2) {
				if (!valid(f + df, nr)) continue;
				to = nr * 8 + f + df;
				if (sign_i(b->sq[to]) == -side || to == b->ep)
					pawn_move(b, moves, cap, &count, from, to);
			}
			continue;
		}
		if (kind == CHESS_KNIGHT) {
			for (int i = 0; i < 8; i++) {
				int nf = f + knight_f[i], nr = r + knight_r[i];
				if (valid(nf, nr) && sign_i(b->sq[nr * 8 + nf]) != side)
					emit(b, moves, cap, &count, from, nr * 8 + nf, 0);
			}
			continue;
		}
		for (int d = 0; d < 8; d++) {
			if (kind == CHESS_BISHOP && !(d & 1)) continue;
			if (kind == CHESS_ROOK && (d & 1)) continue;
			int nf = f + ray_f[d], nr = r + ray_r[d];
			while (valid(nf, nr)) {
				int to = nr * 8 + nf;
				if (sign_i(b->sq[to]) == side) break;
				emit(b, moves, cap, &count, from, to, 0);
				if (b->sq[to] || kind == CHESS_KING) break;
				nf += ray_f[d];
				nr += ray_r[d];
			}
		}
		if (kind != CHESS_KING || chess_in_check(b, side)) continue;
		int home = side == CHESS_WHITE ? 60 : 4;
		if (from != home) continue;
		int rights = side == CHESS_WHITE ? 1 : 4;
		if ((b->castle & rights) && b->sq[home + 3] == side * CHESS_ROOK &&
		    !b->sq[home + 1] && !b->sq[home + 2] &&
		    !attacked(b, home + 1, -side) && !attacked(b, home + 2, -side))
			emit(b, moves, cap, &count, from, home + 2, 0);
		if ((b->castle & (rights << 1)) && b->sq[home - 4] == side * CHESS_ROOK &&
		    !b->sq[home - 1] && !b->sq[home - 2] && !b->sq[home - 3] &&
		    !attacked(b, home - 1, -side) && !attacked(b, home - 2, -side))
			emit(b, moves, cap, &count, from, home - 2, 0);
	}
	return count;
}

static int insufficient(const chess_board_t *b)
{
	int minors = 0, bishops_light = 0, bishops_dark = 0;
	for (int i = 0; i < 64; i++) {
		int p = abs_i(b->sq[i]);
		if (!p || p == CHESS_KING) continue;
		if (p == CHESS_PAWN || p == CHESS_ROOK || p == CHESS_QUEEN) return 0;
		minors++;
		if (p == CHESS_KNIGHT && minors > 1) return 0;
		if (p == CHESS_BISHOP) {
			if (((i / 8 + i % 8) & 1) == 0) bishops_light++;
			else bishops_dark++;
		}
	}
	return minors <= 1 || (minors == bishops_light + bishops_dark &&
			     (!bishops_light || !bishops_dark));
}

int chess_status(const chess_board_t *b)
{
	chess_move_t moves[CHESS_MAX_MOVES];
	if (!chess_legal_moves(b, moves, CHESS_MAX_MOVES))
		return chess_in_check(b, b->side) ? CHESS_CHECKMATE : CHESS_STALEMATE;
	if (b->halfmove >= 100) return CHESS_FIFTY_MOVES;
	if (insufficient(b)) return CHESS_INSUFFICIENT;
	return CHESS_PLAYING;
}

uint32_t chess_key(const chess_board_t *b)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < 64; i++) h = (h ^ (uint8_t)(b->sq[i] + 7)) * 16777619u;
	h = (h ^ (uint8_t)(b->side + 1)) * 16777619u;
	h = (h ^ b->castle) * 16777619u;
	return (h ^ (uint8_t)(b->ep + 1)) * 16777619u;
}

static int evaluate(const chess_board_t *b)
{
	static const int value[7] = { 0, 100, 315, 330, 500, 900, 0 };
	int score = 0;
	for (int i = 0; i < 64; i++) {
		int p = b->sq[i], side = sign_i(p);
		if (!side) continue;
		int f = i % 8, r = i / 8, kind = abs_i(p);
		int center = 6 - abs_i(2 * f - 7) - abs_i(2 * r - 7);
		int bonus = (kind == CHESS_KNIGHT || kind == CHESS_BISHOP) ? 4 * center : 0;
		if (kind == CHESS_PAWN) bonus += (side == CHESS_WHITE ? 6 - r : r - 1) * 5;
		score += side * (value[kind] + bonus);
	}
	return score * b->side;
}

static int search(const chess_board_t *b, int depth, int alpha, int beta, int ply)
{
	chess_move_t moves[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, moves, CHESS_MAX_MOVES);
	if (!n) return chess_in_check(b, b->side) ? -100000 + ply : 0;
	if (b->halfmove >= 100 || insufficient(b)) return 0;
	if (!depth) return evaluate(b);
	int best = -200000;
	for (int i = 0; i < n; i++) {
		chess_board_t next;
		chess_apply(&next, b, moves[i]);
		int score = -search(&next, depth - 1, -beta, -alpha, ply + 1);
		if (score > best) best = score;
		if (score > alpha) alpha = score;
		if (alpha >= beta) break;
	}
	return best;
}

chess_move_t chess_best_move(const chess_board_t *b, uint32_t seed)
{
	chess_move_t moves[CHESS_MAX_MOVES];
	chess_move_t best = { 0, 0, 0 };
	int n = chess_legal_moves(b, moves, CHESS_MAX_MOVES);
	int best_score = -200000, ties = 0;
	for (int i = 0; i < n; i++) {
		chess_board_t next;
		chess_apply(&next, b, moves[i]);
		int score = -search(&next, 1, -200000, 200000, 1);
		if (score > best_score) {
			best_score = score;
			best = moves[i];
			ties = 1;
		} else if (score == best_score && ++ties && (seed = seed * 1664525u +
									1013904223u) % (uint32_t)ties == 0) {
			best = moves[i];
		}
	}
	return best;
}
