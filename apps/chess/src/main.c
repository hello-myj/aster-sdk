/* SPDX-License-Identifier: MIT
 * Touch chess: player is white, Aster's small local search plays black.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster/audio.h>
#include <aster_text.h>
#include "chess.h"

#define HISTORY_MAX 256
#define FACE ((int)ASTER_TEXT_FACES[0].px)
#define BG       ASTER_RGB565(20, 25, 27)
#define LIGHT    ASTER_RGB565(237, 238, 218)
#define DARK     ASTER_RGB565(112, 137, 103)
#define LAST_L   ASTER_RGB565(219, 225, 143)
#define LAST_D   ASTER_RGB565(159, 178, 91)
#define SELECT_L ASTER_RGB565(248, 226, 102)
#define SELECT_D ASTER_RGB565(205, 184, 75)
#define HINT     ASTER_RGB565(56, 87, 57)
#define WHITE    ASTER_RGB565(250, 249, 238)
#define BLACK    ASTER_RGB565(25, 33, 32)
#define HILITE   ASTER_RGB565(250, 247, 222)
#define TEXT     ASTER_RGB565(226, 235, 225)
#define MUTED    ASTER_RGB565(142, 160, 148)
#define CHECK    ASTER_RGB565(211, 87, 78)

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[512];
static uint8_t arena[512];
static uint32_t W, H;
static int cell, board_x, board_y;
static chess_board_t board;
static chess_board_t history[HISTORY_MAX];
static chess_move_t move_history[HISTORY_MAX];
static uint32_t keys[HISTORY_MAX + 1];
static int history_n;
static chess_move_t last_move;
static int has_last;
static int selected = -1;
static uint8_t destinations[64];
static int result;
static int promotion_from = -1, promotion_to = -1;
static int ai_pending;
static uint32_t ai_due;
static int32_t down_x = -1, down_y = -1;
static int32_t move_sound;

static void play_move_sound(void)
{
	if (move_sound > 0) {
		int32_t rc = aster_audio_effect_play((uint32_t)move_sound);
		if (rc != ASTER_OK)
			aster_logf(ASTER_LOG_ERROR, "chess move sound: %d", rc);
	}
}

static int abs_i(int n) { return n < 0 ? -n : n; }

static void text_at(int x, int baseline, uint16_t color, const char *s)
{
	(void)aster_ge_text(&ge, x, baseline, FACE, color, ASTER_GFX_NOBG, s);
}

static void centered(int cx, int baseline, uint16_t color, const char *s)
{
	int w = aster_gfx_text_w(FACE, s, (uint32_t)strlen(s));
	if (w > 0) text_at(cx - w / 2, baseline, color, s);
}

static void box(int x, int y, int w, int h, uint16_t outer, uint16_t inner)
{
	aster_ge_rect(&ge, x, y, (uint32_t)w, (uint32_t)h, outer);
	if (w > 4 && h > 4)
		aster_ge_rect(&ge, x + 2, y + 2, (uint32_t)(w - 4),
				(uint32_t)(h - 4), inner);
}

static void disc(int x, int y, int rx, int ry, uint16_t outer, uint16_t inner)
{
	aster_ge_ellipse(&ge, x, y, (uint32_t)rx, (uint32_t)ry, outer, 0);
	if (rx > 2 && ry > 2)
		aster_ge_ellipse(&ge, x, y, (uint32_t)(rx - 2),
				 (uint32_t)(ry - 2), inner, 0);
}

static void piece(int sq, int p)
{
	int x = board_x + (sq % 8) * cell + cell / 2;
	int y = board_y + (sq / 8) * cell;
	uint16_t edge = p > 0 ? BLACK : HILITE;
	uint16_t fill = p > 0 ? WHITE : BLACK;
	int kind = abs_i(p);

	/* All pieces share a wide base. The different crowns keep them legible
	 * at 28 pixels without relying on optional Unicode chess glyphs. */
	box(x - 9, y + 21, 18, 4, edge, fill);
	switch (kind) {
	case CHESS_PAWN:
		disc(x, y + 9, 5, 5, edge, fill);
		aster_ge_tri(&ge, x - 6, y + 20, x + 6, y + 20,
				 x, y + 11, edge);
		aster_ge_tri(&ge, x - 4, y + 19, x + 4, y + 19,
				 x, y + 13, fill);
		disc(x, y + 17, 4, 4, edge, fill);
		break;
	case CHESS_ROOK:
		box(x - 7, y + 9, 14, 13, edge, fill);
		for (int i = -1; i <= 1; i++)
			box(x + i * 6 - 2, y + 5, 4, 6, edge, fill);
		break;
	case CHESS_KNIGHT:
		aster_ge_tri(&ge, x - 7, y + 21, x + 7, y + 21,
				 x + 4, y + 7, edge);
		aster_ge_tri(&ge, x - 5, y + 20, x + 5, y + 20,
				 x + 3, y + 9, fill);
		aster_ge_tri(&ge, x - 7, y + 14, x + 3, y + 6,
				 x + 5, y + 16, fill);
		aster_ge_rect(&ge, x - 9, y + 13, 8, 4, edge);
		disc(x - 2, y + 11, 2, 2, edge, fill);
		break;
	case CHESS_BISHOP:
		disc(x, y + 12, 6, 9, edge, fill);
		aster_ge_line_aa(&ge, x - 3, y + 16, x + 3, y + 8, edge);
		box(x - 6, y + 19, 12, 3, edge, fill);
		break;
	case CHESS_QUEEN:
		for (int i = -1; i <= 1; i++) disc(x + i * 7, y + 6, 3, 3, edge, fill);
		aster_ge_tri(&ge, x - 8, y + 9, x + 8, y + 9,
				 x, y + 21, edge);
		aster_ge_tri(&ge, x - 6, y + 11, x + 6, y + 11,
				 x, y + 19, fill);
		box(x - 7, y + 14, 14, 7, edge, fill);
		break;
	case CHESS_KING:
		box(x - 6, y + 12, 12, 10, edge, fill);
		aster_ge_rect(&ge, x - 2, y + 3, 4, 11, edge);
		aster_ge_rect(&ge, x - 6, y + 6, 12, 4, edge);
		break;
	}
}

static void name_move(chess_move_t m, char out[8])
{
	out[0] = (char)('a' + m.from % 8);
	out[1] = (char)('8' - m.from / 8);
	out[2] = (char)('a' + m.to % 8);
	out[3] = (char)('8' - m.to / 8);
	out[4] = m.promote ? "  NBRQ"[m.promote] : '\0';
	out[m.promote ? 5 : 4] = '\0';
}

static void draw(void)
{
	aster_ge_clear(&ge, BG);
	text_at(8, 26, TEXT, "CHESS");
	text_at((int)W - 68, 26, MUTED, "vs AI");
	int check = chess_in_check(&board, board.side);
	for (int sq = 0; sq < 64; sq++) {
		int x = board_x + (sq % 8) * cell;
		int y = board_y + (sq / 8) * cell;
		int light = ((sq / 8 + sq % 8) & 1) == 0;
		uint16_t color = light ? LIGHT : DARK;
		if (has_last && (sq == last_move.from || sq == last_move.to))
			color = light ? LAST_L : LAST_D;
		if (sq == selected) color = light ? SELECT_L : SELECT_D;
		if (check && board.sq[sq] == board.side * CHESS_KING)
			color = CHECK;
		aster_ge_rect(&ge, x, y, (uint32_t)cell, (uint32_t)cell, color);
		if (selected >= 0 && destinations[sq]) {
			if (board.sq[sq]) {
				aster_ge_rect_r(&ge, x + 2, y + 2,
					(uint32_t)(cell - 4), (uint32_t)(cell - 4),
					HINT, 5, 1);
			} else {
				aster_ge_ellipse(&ge, x + cell / 2, y + cell / 2,
					3, 3, HINT, 0);
			}
		}
		if (board.sq[sq]) piece(sq, board.sq[sq]);
	}
	int bottom = board_y + 8 * cell;
	const char *status = "Your move";
	if (result == CHESS_CHECKMATE) status = board.side == CHESS_WHITE ? "AI wins" : "You win";
	else if (result) status = "Draw";
	else if (ai_pending) status = "AI thinking";
	else if (check) status = "Check!";
	text_at(8, bottom + 21, result == CHESS_CHECKMATE ? CHECK : TEXT, status);
	if (has_last) {
		char move[8];
		name_move(last_move, move);
		int tw = aster_gfx_text_w(FACE, move, (uint32_t)strlen(move));
		if (tw > 0) text_at((int)W - 8 - tw, bottom + 21, MUTED, move);
	}
	aster_ge_rect_r(&ge, 8, bottom + 25, 72, 26, DARK, 5, 0);
	aster_ge_rect_r(&ge, 88, bottom + 25, 72, 26, DARK, 5, 0);
	centered(44, bottom + 46, WHITE, "NEW");
	centered(124, bottom + 46, WHITE, "UNDO");
	if (promotion_from >= 0) {
		int px = ((int)W - 208) / 2, py = board_y + cell * 3;
		aster_ge_rect_r(&ge, px - 4, py - 31, 216, 89, BG, 8, 0);
		centered((int)W / 2, py - 8, TEXT, "PROMOTE");
		const int choice[4] = { CHESS_QUEEN, CHESS_ROOK,
					CHESS_BISHOP, CHESS_KNIGHT };
		for (int i = 0; i < 4; i++) {
			int bx = px + i * 52;
			aster_ge_rect(&ge, bx, py, 48, 48, (i & 1) ? DARK : LIGHT);
			/* A compact letter remains clear even when the piece art is small. */
			const char *label = choice[i] == CHESS_QUEEN ? "Q" :
				choice[i] == CHESS_ROOK ? "R" :
				choice[i] == CHESS_BISHOP ? "B" : "N";
			centered(bx + 24, py + 34, BLACK, label);
		}
	}
	aster_ge_present(&ge);
}

static void select_square(int sq)
{
	selected = sq;
	for (int i = 0; i < 64; i++) destinations[i] = 0;
	if (sq >= 0) {
		chess_move_t moves[CHESS_MAX_MOVES];
		int n = chess_legal_moves(&board, moves, CHESS_MAX_MOVES);
		for (int i = 0; i < n; i++) {
			if (moves[i].from == sq) destinations[moves[i].to] = 1;
		}
	}
}

static void refresh_result(void)
{
	result = chess_status(&board);
	if (result || history_n < 4) return;
	uint32_t key = keys[history_n];
	int same = 0;
	for (int i = 0; i <= history_n; i++) same += keys[i] == key;
	if (same >= 3) result = CHESS_STALEMATE;
}

static void play_move(chess_move_t move)
{
	if (history_n == HISTORY_MAX) {
		for (int i = 1; i < HISTORY_MAX; i++) {
			history[i - 1] = history[i];
			move_history[i - 1] = move_history[i];
			keys[i - 1] = keys[i];
		}
		history_n--;
		keys[history_n] = chess_key(&board);
	}
	history[history_n] = board;
	move_history[history_n++] = move;
	chess_board_t next;
	chess_apply(&next, &board, move);
	board = next;
	keys[history_n] = chess_key(&board);
	last_move = move;
	has_last = 1;
	select_square(-1);
	promotion_from = promotion_to = -1;
	refresh_result();
	play_move_sound();
	if (!result && board.side == CHESS_BLACK) {
		ai_pending = 1;
		ai_due = aster_time_now_ms() + 120;
	}
}

static void new_game(void)
{
	chess_reset(&board);
	history_n = 0;
	keys[0] = chess_key(&board);
	has_last = 0;
	result = 0;
	ai_pending = 0;
	promotion_from = promotion_to = -1;
	select_square(-1);
}

static void undo_turn(void)
{
	if (!history_n) return;
	int steps = board.side == CHESS_WHITE && history_n >= 2 ? 2 : 1;
	history_n -= steps;
	board = history[history_n];
	result = 0;
	ai_pending = 0;
	promotion_from = promotion_to = -1;
	has_last = history_n > 0;
	if (has_last) last_move = move_history[history_n - 1];
	select_square(-1);
	refresh_result();
}

static void tap(int x, int y)
{
	int bottom = board_y + 8 * cell;
	if (promotion_from >= 0) {
		int px = ((int)W - 208) / 2, py = board_y + cell * 3;
		if (y >= py && y < py + 48 && x >= px && x < px + 208) {
			int i = (x - px) / 52;
			static const uint8_t promote[4] = {
				CHESS_QUEEN, CHESS_ROOK, CHESS_BISHOP, CHESS_KNIGHT
			};
			chess_move_t m = { (uint8_t)promotion_from,
				(uint8_t)promotion_to, promote[i] };
			play_move(m);
		}
		return;
	}
	if (y >= bottom + 25 && y < bottom + 51) {
		if (x >= 8 && x < 80) new_game();
		else if (x >= 88 && x < 160) undo_turn();
		return;
	}
	if (result || ai_pending || board.side != CHESS_WHITE ||
	    x < board_x || x >= board_x + 8 * cell ||
	    y < board_y || y >= bottom) return;
	int sq = ((y - board_y) / cell) * 8 + (x - board_x) / cell;
	if (selected >= 0 && destinations[sq]) {
		chess_move_t moves[CHESS_MAX_MOVES];
		int n = chess_legal_moves(&board, moves, CHESS_MAX_MOVES);
		for (int i = 0; i < n; i++) {
			if (moves[i].from == selected && moves[i].to == sq) {
				if (moves[i].promote) {
					promotion_from = selected;
					promotion_to = sq;
				} else play_move(moves[i]);
				return;
			}
		}
	}
	select_square(board.sq[sq] > 0 ? sq : -1);
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = {
		.size = sizeof(d), .version = 1, .format = ASTER_PIXEL_NATIVE,
		.flags = ASTER_SURFACE_F_PANEL_CANVAS,
	};
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	int32_t err = aster_surface_create(&d, sizeof(d), &surf);
	if (err != ASTER_OK) return err;
	err = aster_surface_acquire(surf, &buf, sizeof(buf));
	if (err != ASTER_OK) return err;
	W = buf.width;
	H = buf.height;
	cell = ((int)W - 16) / 8;
	if (cell > ((int)H - 96) / 8) cell = ((int)H - 96) / 8;
	if (cell < 20) return ASTER_EINVAL;
	board_x = ((int)W - 8 * cell) / 2;
	board_y = 42;
	aster_ge_init(&ge, surf, W, H, ring, sizeof(ring) / sizeof(ring[0]),
		      arena, sizeof(arena));
	if (aster_system_has_feature(ASTER_FEATURE_AUDIO_OUTPUT) > 0) {
		const char *path = "/assets/audio/move.wav";
		move_sound = aster_audio_effect_load(path, (uint32_t)strlen(path));
		if (move_sound <= 0) {
			aster_logf(ASTER_LOG_ERROR, "chess sound load: %d", move_sound);
			move_sound = 0;
		}
	}
	aster_app_set_framerate(8);
	new_game();
	aster_logf(ASTER_LOG_INFO, "chess %ux%u cell=%d", W, H, cell);
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	if (move_sound > 0) {
		(void)aster_audio_effect_unload((uint32_t)move_sound);
		move_sound = 0;
	}
	if (surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(surf);
		surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	if (ev->type == ASTER_EVENT_TOUCH) {
		int x = (int)ev->u.touch.x, y = (int)ev->u.touch.y;
		if (ev->u.touch.action == ASTER_TOUCH_DOWN) {
			down_x = x;
			down_y = y;
		} else if (ev->u.touch.action == ASTER_TOUCH_UP && down_x >= 0) {
			if (abs_i(x - down_x) < 18 && abs_i(y - down_y) < 18)
				tap(x, y);
			down_x = down_y = -1;
		}
	} else if (ev->type == ASTER_EVENT_FRAME) {
		if (ai_pending && (int32_t)(aster_time_now_ms() - ai_due) >= 0) {
			ai_pending = 0;
			chess_move_t m = chess_best_move(&board, aster_time_now_ms());
			if (m.from != m.to) play_move(m);
		}
		draw();
	}
}
