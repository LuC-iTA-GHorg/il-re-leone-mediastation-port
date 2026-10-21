/* "Stalking Zazu" (actor type 0x0f): the minigame of The Lion King where Simba creeps up on
 * Zazu, hiding behind rocks while Zazu looks around. Native code in the player; checked
 * function by function against LIONKING.EXE (class at 1188:6986, vtable 1188:708a, object
 * size 0x9a). ScummVM's engines/mediastation/minigames/stalkingzazu.cpp was the starting
 * point; the differences found are marked "exe:".
 *
 * The actor draws nothing itself (vtable +0x20 answers "not visible", 1188:75a0): it moves the
 * Simba sprite and the rock actors over a grid of rows x columns cells covering its bounding
 * box. Holding the mouse button down makes Simba crouch; while the button is up Simba walks
 * towards the mouse at every timer tick. If Zazu looks while Simba stands and no rock is in
 * the cell north of him, the game is lost; reaching the target at the top centre wins.
 *
 * Fields of the original object used below: +0x34 all the rocks, +0x3c the rocks in play,
 * +0x44 the grid (cells hold an index into +0x3c, 1-based), +0x48 timer, +0x4c tick period,
 * +0x54 Simba, +0x56 clips by (direction, crouched), +0x5e Simba position, +0x62 direction,
 * +0x64 crouched clip shown, +0x66/+0x68 sounds, +0x6a audio enabled, +0x6c step length,
 * +0x72 target, +0x76 success radius, +0x78 where Simba's centre may go, +0x80 timer machine,
 * +0x86 game machine, +0x8c Zazu looking, +0x8e active, +0x90 only Simba takes the mouse,
 * +0x92 cursor, +0x94 rocks shown at load, +0x96 that parameter given, +0x98 crouched event
 * sent. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script.h"

/* directions (1188:9d10 maps the tokens 0x44c..0x453) */
enum { D_NONE, D_EAST, D_NORTH_EAST, D_NORTH, D_NORTH_WEST, D_WEST, D_SOUTH_WEST, D_SOUTH, D_SOUTH_EAST };

/* states and events of the two machines (tables built by 1188:8bd6 and 1188:8e22) */
enum { GS_START, GS_RESET, GS_CROUCHED, GS_CAN_MOVE, GS_PROCESS_MOVE, GS_DEFEAT, GS_SUCCESS };
enum { GE_RESET, GE_CROUCH, GE_UNCROUCH, GE_MOVE, GE_LOSE, GE_WIN };
enum { TS_EXPIRED, TS_QUEUED, TS_EXPIRED_INACTIVE, TS_EXPIRED_ACTIVE, TS_QUEUED_INACTIVE };
enum { TE_ACTIVATE, TE_INACTIVATE, TE_EXPIRE, TE_QUEUE };

/* Next state for [state][event]; -2 means "ignore the event" (the exe maps -2 to the
 * sentinel of 1188:f286, which 1188:c690 skips silently; -1, never used here, would call the
 * machine's error handler 1188:8a5c / 1188:8962, which throws). */
static const int8_t timer_table[5][4] = {
	{-2, 2, -2, 1}, /* expired */
	{-2, 4, 0, -2}, /* queued */
	{3, -2, -2, 4}, /* expired, inactive */
	{-2, 2, -2, 1}, /* expired, active */
	{1, -2, -2, -2} /* queued, inactive */
};
static const int8_t game_table[7][6] = {
	{1, -2, -2, -2, -2, -2}, /* start */
	{-2, 2, -2, -2, -2, -2}, /* reset: only crouch (a second reset is ignored) */
	{1, -2, 3, -2, -2, -2},  /* crouched */
	{1, 2, -2, 4, 5, 6},     /* can move */
	{1, 2, -2, 4, 5, 6},     /* process move */
	{1, -2, -2, -2, -2, -2}, /* defeat */
	{1, -2, -2, -2, -2, -2}  /* success */
};

typedef struct {
	struct {
		int ev, arg;
	} events[32];
	int nevents;
	int state;
	int handling;
} Machine;

typedef struct {
	int16_t r, c; /* row, column (word 0 and word 1 of the exe's cell) */
} Cell;

typedef struct {
	/* parameters */
	uint16_t cursor;
	double period_s;
	int active;
	uint16_t simba;
	uint16_t clips[18]; /* index = direction (+9 crouched) */
	uint16_t rows, cols;
	uint16_t rocks[64]; /* +0x34 */
	int nrocks;
	int16_t step, success_radius;
	uint16_t sound1, sound2;
	int sound_enabled;
	int16_t rocks_to_show;
	int rocks_to_show_given;

	/* state */
	uint16_t inplay[64]; /* +0x3c */
	int ninplay;
	uint16_t *field; /* rows*cols, 1-based index into inplay or 0 */
	MsPoint field_size;
	MsPoint target, simba_pos;
	MsRect bounds; /* where Simba's centre may move, in field coordinates */
	int direction;
	int crouch_clip_shown, accepting_mouse, zazu_looking, crouched_sent;
	int mouse_down, mouse_inside;
	Machine game, timer;
} Zazu;

#define Z(a) ((Zazu *)(a)->x)

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void trace(const char *fmt, ...) {
	if (!script_trace)
		return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[%6u] zazu ", rt_now());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* constructor 1188:6986 */
static void zazu_init(MsActor *a) {
	Zazu *z = Z(a);
	z->period_s = 0.1; /* DS:2E4E */
	z->step = 10;
	z->success_radius = 20;
	z->accepting_mouse = 1;
	z->game.state = GS_START;
	z->timer.state = TS_EXPIRED;
}

/* 1188:9d10; other tokens leave the direction at 0 */
static int direction_of_token(uint16_t t) {
	static const int dirs[8] = {D_WEST, D_SOUTH_WEST, D_SOUTH, D_SOUTH_EAST, D_EAST, D_NORTH_EAST, D_NORTH,
		D_NORTH_WEST};
	return t >= 0x44c && t <= 0x453 ? dirs[t - 0x44c] : D_NONE;
}

/* 1188:9bdc (next) and 1188:9b42 (opposite); 0 stays 0 */
static int rotate(int d, int steps) {
	return d == D_NONE ? D_NONE : (d - 1 + steps) % 8 + 1;
}

static int clip_index(int d, int crouched) {
	return d + (crouched ? 9 : 0);
}

/* 1188:6cd2 */
static int zazu_param(MsActor *a, MsChunk *c, uint16_t p) {
	Zazu *z = Z(a);
	switch (p) {
	case AH_CURSOR_RESOURCE_ID: /* +0x92 */
		z->cursor = ms_t_u16(c);
		return 1;
	case AH_FRAME_RATE: { /* +0x4c = 1 / frame rate */
		double fps = ms_t_double(c);
		if (fps > 0) /* the exe divides blindly */
			z->period_s = 1.0 / fps;
		return 1;
	}
	case AH_EDITABLE: /* +0x8e */
		z->active = ms_t_u8(c) != 0;
		return 1;
	case AH_ZAZU_SPRITE_ID: /* +0x54 */
		z->simba = ms_t_u16(c);
		return 1;
	case AH_ZAZU_DIRECTIONS:
		for (int i = 0; i < 5; i++) {
			int d = direction_of_token(ms_t_u16(c));
			uint16_t standing = ms_t_u16(c), crouched = ms_t_u16(c);
			z->clips[clip_index(d, 0)] = standing;
			z->clips[clip_index(d, 1)] = crouched;
		}
		return 1;
	case AH_ZAZU_FIELD:
		/* the grid covers the bounding box read before this parameter; the target is the
		 * top centre (box width / 2, 0) */
		z->rows = ms_t_u16(c);
		z->cols = ms_t_u16(c);
		z->field_size.x = a->box.w;
		z->field_size.y = a->box.h;
		free(z->field);
		z->field = calloc((size_t)z->rows * z->cols + 1, sizeof(uint16_t));
		z->target.x = (int16_t)(a->box.w / 2);
		z->target.y = 0;
		return 1;
	case AH_ZAZU_OBSTACLE_ACTOR_ID: {
		uint16_t id = ms_t_u16(c);
		if (z->nrocks < 64)
			z->rocks[z->nrocks++] = id;
		return 1;
	}
	case AH_ZAZU_UNK_X: /* +0x6c, length of one step */
		z->step = (int16_t)ms_t_gunit(c);
		return 1;
	case AH_ZAZU_UNK_Y: /* +0x76 */
		z->success_radius = (int16_t)ms_t_gunit(c);
		return 1;
	case AH_ZAZU_UNK_SOUND1: /* +0x66: played at every step */
		z->sound1 = ms_t_u16(c);
		return 1;
	case AH_ZAZU_UNK_SOUND2: /* +0x68: played when Simba crouches or gets up */
		z->sound2 = ms_t_u16(c);
		return 1;
	case AH_ZAZU_AUDIO_ENABLED:
		z->sound_enabled = ms_t_u8(c) != 0;
		return 1;
	case AH_ZAZU_OBSTACLES_TO_SHOW: /* +0x94, and +0x96 = 1 */
		z->rocks_to_show = ms_t_s16(c);
		z->rocks_to_show_given = 1;
		return 1;
	case AH_ZAZU_UNK_POINT:
		/* exe: read into +0x6e, then the chunk also goes to the base class reader
		 * (1190:9ad2); not in the Lion King data */
		ms_t_point(c);
		return 1;
	}
	return 0;
}

/* ---- the grid (class at 1188:9d8c: +4 rows, +6 columns, +8 cells, +0xc size) ---- */

static int in_bounds(Zazu *z, Cell c) { /* 1188:b3bc, unsigned compares */
	return (uint16_t)c.r < z->rows && (uint16_t)c.c < z->cols;
}

static uint16_t *at(Zazu *z, Cell c) { /* 1188:905a / 1188:a466 */
	return &z->field[c.r * z->cols + c.c];
}

/* 1188:b1fc: the neighbour in a direction; 1 (and no move) when it would leave the grid
 * at the top or on the left, or for direction 0 */
static int adjacent(Cell c, int d, Cell *out) {
	*out = c;
	switch (d) {
	case D_NONE:
		return 1;
	case D_EAST:
		out->c++;
		break;
	case D_NORTH_EAST:
		if (c.r == 0)
			return 1;
		out->r--;
		out->c++;
		break;
	case D_NORTH:
		if (c.r == 0)
			return 1;
		out->r--;
		break;
	case D_NORTH_WEST:
		if (c.r == 0 || c.c == 0)
			return 1;
		out->r--;
		out->c--;
		break;
	case D_WEST:
		if (c.c == 0)
			return 1;
		out->c--;
		break;
	case D_SOUTH_WEST:
		if (c.c == 0)
			return 1;
		out->c--;
		out->r++;
		break;
	case D_SOUTH:
		out->r++;
		break;
	case D_SOUTH_EAST:
		out->r++;
		out->c++;
		break;
	}
	return 0;
}

/* 1188:9f8c: unsigned integer division */
static MsPoint cell_size(Zazu *z) {
	MsPoint s = {(int16_t)((uint16_t)z->field_size.x / z->cols), (int16_t)((uint16_t)z->field_size.y / z->rows)};
	return s;
}

/* 1188:9dc8: _ftol((col + 1) * W / cols - cell width * 0.5), the same for rows */
static MsPoint center_of_cell(Zazu *z, Cell c) {
	MsPoint s = cell_size(z);
	long double x = (long double)((int32_t)(c.c + 1) * z->field_size.x / (int32_t)z->cols) - s.x * 0.5L;
	long double y = (long double)((int32_t)(c.r + 1) * z->field_size.y / (int32_t)z->rows) - s.y * 0.5L;
	MsPoint p = {(int16_t)(int32_t)x, (int16_t)(int32_t)y};
	return p;
}

/* 1188:b31e: _ftol(rows / H * y) and _ftol(cols / W * x); an index out of range (also a
 * negative one: unsigned compare) becomes the last row / column */
static Cell cell_at(Zazu *z, MsPoint p) {
	long double fr = (long double)z->rows / z->field_size.y * p.y;
	long double fc = (long double)z->cols / z->field_size.x * p.x;
	uint16_t r = (uint16_t)(int32_t)fr, c = (uint16_t)(int32_t)fc;
	if (r >= z->rows)
		r = (uint16_t)(z->rows - 1);
	if (c >= z->cols)
		c = (uint16_t)(z->cols - 1);
	Cell cell = {(int16_t)r, (int16_t)c};
	return cell;
}

/* 1188:9eb2: (W / 2, H - cell height / 2), integer halves */
static MsPoint bottom_center(Zazu *z) {
	MsPoint p = {(int16_t)(z->field_size.x / 2), (int16_t)(z->field_size.y - cell_size(z).y / 2)};
	return p;
}

/* 1188:a2b6: a rock may not touch the border, nor be directly above or below another one;
 * the check follows the chains of neighbouring rocks (all 8 directions from the first cell,
 * then all but the one we came from). */
static int can_place(Zazu *z, Cell pos, int dir, Cell *visited, int *nvisited, int maxvisited) {
	if (*nvisited < maxvisited)
		visited[(*nvisited)++] = pos;
	if (pos.r == 0 || pos.c == 0 || z->rows - pos.r == 1 || z->cols - pos.c == 1 || dir == D_SOUTH ||
		dir == D_NORTH)
		return 0;
	int ok = 1;
	int cur = dir == D_NONE ? D_EAST : rotate(dir, 1), stop = dir == D_NONE ? D_EAST : dir;
	do {
		Cell adj;
		if (!adjacent(pos, cur, &adj) && in_bounds(z, adj) && *at(z, adj) != 0) {
			int seen = 0; /* 1188:ae80 */
			for (int i = 0; i < *nvisited; i++)
				if (visited[i].r == adj.r && visited[i].c == adj.c)
					seen = 1;
			if (!seen)
				ok = can_place(z, adj, rotate(cur, 4), visited, nvisited, maxvisited);
		}
		cur = rotate(cur, 1);
	} while (ok && cur != stop);
	return ok;
}

/* 1188:a130: a random free cell of the row that passes can_place; a cell that does not is
 * removed from the candidates keeping their order (1188:a7ec). 0 = placed (the exe returns
 * 0 on success, 1 on failure). */
static int add_rock_to_row(Zazu *z, uint16_t value, int row) {
	int maxv = z->rows * z->cols + 1;
	Cell *cells = malloc(sizeof(Cell) * (size_t)(z->cols + 1));
	Cell *visited = malloc(sizeof(Cell) * (size_t)maxv);
	int n = 0, placed = 0;
	for (int col = 0; col < z->cols; col++) { /* 1188:a20e */
		Cell c = {(int16_t)row, (int16_t)col};
		if (*at(z, c) == 0)
			cells[n++] = c;
	}
	while (n > 0 && !placed) {
		int i = (int)(script_random() % (uint32_t)n); /* 1188:a163: 1188:d6c2, unsigned long modulo (1180:0e6a) */
		int nv = 0;
		if (can_place(z, cells[i], D_NONE, visited, &nv, maxv)) {
			*at(z, cells[i]) = value;
			placed = 1;
		} else {
			memmove(cells + i, cells + i + 1, sizeof(Cell) * (size_t)(n - i - 1));
			n--;
		}
	}
	free(cells);
	free(visited);
	return placed ? 0 : 1;
}

static int cmp_u16(const void *a, const void *b) { /* 1188:a0ac */
	uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b;
	return x < y ? -1 : x > y;
}

/* 1188:9fe6 with 1188:a0e2: one random bound in 0..n per inner row, sorted; rock k (1-based
 * index) goes into the current row while k < bound, the row changes after each bound or
 * when a rock does not fit (that rock is tried again in the next row). */
static void place_rocks(Zazu *z) {
	int nrows = z->rows - 2;
	if (nrows <= 0)
		return;
	uint16_t *bounds = malloc(sizeof(uint16_t) * (size_t)nrows);
	for (int i = 0; i < nrows; i++)
		bounds[i] = (uint16_t)(script_random() % (uint32_t)(z->ninplay + 1)); /* 1188:a0f5 */
	qsort(bounds, (size_t)nrows, sizeof(uint16_t), cmp_u16);
	int k = 0, row = 1;
	for (int i = 0; i < nrows; i++) {
		while (bounds[i] > k && add_rock_to_row(z, (uint16_t)(k + 1), row) != 1)
			k++;
		row++;
	}
	free(bounds);
}

/* 1188:7eac: the rocks in play are all of them minus n - k, removed at evenly spaced
 * positions from a random start. The positions refer to the full list while the list
 * shrinks: past its end the exe removes the first entry (1188:92de gives an invalid
 * iterator, 1188:9ae4 then shifts the whole list down). */
static void choose_rocks(Zazu *z, int k) {
	int n = z->nrocks;
	memcpy(z->inplay, z->rocks, sizeof(uint16_t) * (size_t)n);
	z->ninplay = n;
	int remove = n - k;
	if (remove == 0)
		return;
	int stride = n / remove;
	int idx = (int)(script_random() % (uint32_t)n); /* 1188:7ee9 */
	for (; remove > 0; remove--) {
		int i = idx < z->ninplay ? idx : 0;
		memmove(z->inplay + i, z->inplay + i + 1, sizeof(uint16_t) * (size_t)(z->ninplay - i - 1));
		z->ninplay--;
		idx = (idx + stride) % n;
	}
}

/* ---- actors controlled by the minigame ---- */

static void call0(uint16_t id, uint32_t m) {
	MsActor *t = god_actor(id);
	if (t) {
		MsValue r = actor_call_method(t, m, NULL, 0);
		val_free(&r);
	}
}

/* 1188:8304: z = bottom of the minigame's box + its z - y (lower in the field = in front),
 * centre on the point, show */
static void place_and_show(MsActor *a, uint16_t id, MsPoint dest) {
	MsActor *t = god_actor(id);
	if (!t) {
		ms_warn("Stalking Zazu: actor %u not loaded", id);
		return;
	}
	int z = a->box.y + a->box.h + a->z - dest.y; /* 1188:8482 */
	if (t->z != z) {                             /* 1190:982a */
		t->z = z;
		rt_stage_resort();
		actor_invalidate(t);
	}
	MsRect b = actor_bbox(t);
	actor_move_to(t, dest.x + a->box.x - b.w / 2, dest.y + a->box.y - b.h / 2);
	call0(id, M_SPATIAL_SHOW);
}

static void set_clip(Zazu *z, uint16_t clip) { /* 1188:845a -> 1068:0bf6 */
	MsActor *s = god_actor(z->simba);
	if (s && s->type == ACTOR_SPRITE) {
		MsValue v = val_token(clip);
		MsValue r = actor_call_method(s, M_SET_CURRENT_CLIP, &v, 1);
		val_free(&r);
	}
}

static void set_crouch_clip(Zazu *z, int crouch) { /* 1188:8534 */
	if (crouch != z->crouch_clip_shown) {
		set_clip(z, z->clips[clip_index(z->direction, crouch)]);
		z->crouch_clip_shown = crouch;
	}
}

static void set_clip_from_direction(Zazu *z, int d) { /* 1188:84d6 */
	if (z->direction != d || z->crouch_clip_shown) {
		set_clip(z, z->clips[clip_index(d, 0)]);
		z->crouch_clip_shown = 0;
		z->direction = d;
	}
}

static void sound_play(uint16_t id) { /* 1188:7332 */
	MsActor *s = god_actor(id);
	if (id && s && s->type == ACTOR_SOUND) {
		MsValue r = actor_call_method(s, M_TIME_PLAY, NULL, 0);
		val_free(&r);
	}
}

static void sound_stop(uint16_t id) { /* 1188:7306 */
	MsActor *s = god_actor(id);
	if (id && s && s->type == ACTOR_SOUND) {
		MsValue r = actor_call_method(s, M_TIME_STOP, NULL, 0);
		val_free(&r);
	}
}

/* ---- rules ---- */

/* 1188:8602: a rock in the cell north of Simba */
static int behind_rock(Zazu *z) {
	Cell n;
	if (adjacent(cell_at(z, z->simba_pos), D_NORTH, &n))
		return 0;
	return *at(z, n) != 0;
}

/* 1188:8586 */
static int in_success_position(Zazu *z) {
	int32_t dx = z->target.x - z->simba_pos.x, dy = z->target.y - z->simba_pos.y;
	return sqrt((double)(dx * dx + dy * dy)) <= z->success_radius;
}

/* atan2 in degrees as the exe computes it: the x87 result times 57.29577950560105 (DS:2EA6,
 * slightly less than 180/pi: +-180 and +-90 are never reached exactly) */
static double angle_of(int dx, int dy) {
	return (double)(atan2l((long double)dy, (long double)dx) * 57.29577950560105L);
}

/* 1188:7b88: direction of a walk; 0 if no range matches */
static int direction_of_vector(int dx, int dy) {
	static const struct {
		double lo, hi;
		int dir;
	} ranges[6] = {{90.0, 180.0, D_WEST}, {-22.5, 90.0, D_EAST}, {-67.5, -22.5, D_NORTH_EAST},
		{-112.5, -67.5, D_NORTH}, {-157.5, -112.5, D_NORTH_WEST}, {-180.0, -157.5, D_WEST}};
	double a = angle_of(dx, dy);
	for (int i = 0; i < 6; i++)
		if (ranges[i].lo <= a && a < ranges[i].hi)
			return ranges[i].dir;
	return D_NONE;
}

/* 1188:79e8: one step of at most "step" pixels towards the mouse (components truncated by
 * _ftol), never downwards, then kept inside the bounds (right and bottom edges excluded,
 * 1190:6c8c) */
static MsPoint desired_position(Zazu *z, int vx, int vy) {
	double len = (double)sqrtl((long double)((int32_t)vy * vy + (int32_t)vx * vx));
	double scale = 0.0;
	if (len != 0.0)
		scale = (double)((long double)z->step / len);
	int dx = (int16_t)(int32_t)((long double)vx * scale), dy = (int16_t)(int32_t)((long double)vy * scale);
	if (dy >= 0)
		dy = 0;
	else if (vy >= dy)
		dy = vy;
	if (dx > 0) {
		if (vx <= dx)
			dx = vx;
	} else if (vx >= dx) {
		dx = vx;
	}
	int x = z->simba_pos.x + dx, y = z->simba_pos.y + dy;
	MsRect b = z->bounds;
	if (x >= b.x + b.w)
		x = b.x + b.w - 1;
	if (x < b.x)
		x = b.x;
	if (y < b.y)
		y = b.y;
	if (y >= b.y + b.h)
		y = b.y + b.h - 1;
	MsPoint p = {(int16_t)x, (int16_t)y};
	return p;
}

/* 1188:7684: the four corners of the rectangle (x - w/4, y - h/4, w/2, h/4) around the new
 * position (w, h: cell size), clipped to the bounds, must all be in free cells. exe: the
 * rectangle is only a quarter cell high and ends at y; its right and bottom edges are
 * included as corners. */
static int clear_at(Zazu *z, MsPoint p) {
	MsPoint cs = cell_size(z);
	int hw = (int)(int32_t)(cs.x * 0.25), hh = (int)(int32_t)(cs.y * 0.25);
	int l = p.x - hw, t = p.y - hh, r = l + 2 * hw, b = t + hh;
	MsRect mb = z->bounds; /* 1190:7220: intersection */
	if (l < mb.x)
		l = mb.x;
	if (t < mb.y)
		t = mb.y;
	if (r > mb.x + mb.w)
		r = mb.x + mb.w;
	if (b > mb.y + mb.h)
		b = mb.y + mb.h;
	if (l >= r || t >= b)
		l = t = r = b = 0; /* no intersection: empty rectangle at 0,0 */
	MsPoint corners[4] = {{(int16_t)l, (int16_t)t}, {(int16_t)l, (int16_t)b}, {(int16_t)r, (int16_t)b},
		{(int16_t)r, (int16_t)t}};
	for (int i = 0; i < 4; i++)
		if (*at(z, cell_at(z, corners[i])) != 0)
			return 0;
	return 1;
}

/* 1188:75b6 with 1188:7922 */
static void process_move(MsActor *a, int mx, int my) {
	Zazu *z = Z(a);
	int vx = mx - a->box.x - z->simba_pos.x, vy = my - a->box.y - z->simba_pos.y;
	if (vx == 0 && vy == 0)
		return;
	int d = direction_of_vector(vx, vy);
	MsPoint next = desired_position(z, vx, vy);
	if (next.x == z->simba_pos.x && next.y == z->simba_pos.y)
		return;
	set_clip_from_direction(z, d);
	if (clear_at(z, next)) {
		place_and_show(a, z->simba, next);
		MsActor *s = god_actor(z->simba);
		if (s && s->type == ACTOR_SPRITE) { /* 1188:8434 -> 1068:0b7c */
			MsValue r = actor_call_method(s, M_INCREMENT_FRAME, NULL, 0);
			val_free(&r);
		}
		z->simba_pos = next;
		trace("simba %d,%d", next.x, next.y);
	}
}

/* ---- state machines (1188:c370 queues, 1188:c61e runs, 1188:c448 one step) ---- */

static void game_enter(MsActor *a, int state, int arg);
static void timer_enter(MsActor *a, int state);

static void machine_queue(Machine *m, int ev, int arg) {
	if (m->nevents < 32) {
		m->events[m->nevents].ev = ev;
		m->events[m->nevents].arg = arg;
		m->nevents++;
	}
}

static void run_game(MsActor *a) { /* 1188:86d2 */
	Machine *m = &Z(a)->game;
	if (m->handling)
		return;
	m->handling = 1;
	while (m->nevents > 0) {
		int ev = m->events[0].ev, arg = m->events[0].arg;
		memmove(m->events, m->events + 1, sizeof(m->events[0]) * (size_t)(m->nevents - 1));
		m->nevents--;
		int next = game_table[m->state][ev];
		if (next < 0)
			continue; /* ignored silently */
		m->state = next;
		game_enter(a, next, arg);
	}
	m->handling = 0;
}

static void run_timer(MsActor *a) { /* 1188:89e4 */
	Machine *m = &Z(a)->timer;
	if (m->handling)
		return;
	m->handling = 1;
	while (m->nevents > 0) {
		int ev = m->events[0].ev;
		memmove(m->events, m->events + 1, sizeof(m->events[0]) * (size_t)(m->nevents - 1));
		m->nevents--;
		int next = timer_table[m->state][ev];
		if (next < 0)
			continue;
		m->state = next;
		timer_enter(a, next);
	}
	m->handling = 0;
}

static void game_event(MsActor *a, int ev, int arg) { /* 1188:869e + 1188:86d2 */
	machine_queue(&Z(a)->game, ev, arg);
	run_game(a);
}

static void timer_event(MsActor *a, int ev) { /* 1188:89b2 + 1188:89e4 */
	machine_queue(&Z(a)->timer, ev, 0);
	run_timer(a);
}

/* 1188:735c */
static void set_zazu_look(MsActor *a, int looking) {
	Zazu *z = Z(a);
	if (z->zazu_looking == looking)
		return;
	z->zazu_looking = looking;
	trace("look %d (simba %d,%d %s)", looking, z->simba_pos.x, z->simba_pos.y,
		behind_rock(z) ? "behind a rock" : "in the open");
	if (looking && !behind_rock(z))
		game_event(a, GE_LOSE, 0);
}

/* 1188:7dc8: n >= 0 chooses min(n, all) rocks and lays them out again; then every rock is
 * hidden, the ones in the grid shown, Simba put back at the bottom centre (+3, 0) crouched
 * facing north */
static void reset_field(MsActor *a, int n) {
	Zazu *z = Z(a);
	if (n >= 0 && z->field) {
		choose_rocks(z, n < z->nrocks ? n : z->nrocks);
		memset(z->field, 0, sizeof(uint16_t) * (size_t)z->rows * z->cols); /* 1188:7e3e */
		place_rocks(z);
	}
	for (int i = 0; i < z->nrocks; i++) /* 1188:7f40 */
		call0(z->rocks[i], M_SPATIAL_HIDE);
	for (int r = 0; r < z->rows && z->field; r++) /* 1188:7fba */
		for (int c = 0; c < z->cols; c++) {
			Cell cell = {(int16_t)r, (int16_t)c};
			uint16_t v = *at(z, cell);
			if (v && v <= z->ninplay) {
				MsPoint p = center_of_cell(z, cell);
				trace("rock %u cell %d,%d at %d,%d", z->inplay[v - 1], r, c, p.x + a->box.x, p.y + a->box.y);
				place_and_show(a, z->inplay[v - 1], p);
			}
		}
	z->simba_pos = bottom_center(z); /* 1188:807e */
	z->simba_pos.x += 3;
	place_and_show(a, z->simba, z->simba_pos);
	set_clip_from_direction(z, D_NORTH);
	set_crouch_clip(z, 1);
	trace("simba %d,%d", z->simba_pos.x, z->simba_pos.y);
	rt_invalidate_mouse();
}

/* state entry handlers of the game machine (table at DS:018C, filled by 1188:8cbe) */
static void game_enter(MsActor *a, int state, int arg) {
	Zazu *z = Z(a);
	switch (state) {
	case GS_START: /* 1188:8700 */
		break;
	case GS_RESET: /* 1188:8704 */
		reset_field(a, (int16_t)arg);
		set_zazu_look(a, 0);
		z->accepting_mouse = 1;
		z->crouched_sent = 0;
		break;
	case GS_CROUCHED: /* 1188:8740 */
		if (!z->crouched_sent) {
			z->crouched_sent = 1;
			rt_invalidate_mouse();
			rt_queue_actor_event(a->id, EV_MINIGAME_CROUCHED, NULL);
		}
		set_crouch_clip(z, 1);
		if (z->sound_enabled)
			sound_play(z->sound2);
		z->accepting_mouse = 0;
		break;
	case GS_CAN_MOVE: /* 1188:87de: exe: no timer after a defeat */
		set_crouch_clip(z, 0);
		if (z->sound_enabled)
			sound_play(z->sound2);
		if (z->zazu_looking && !behind_rock(z))
			game_event(a, GE_LOSE, 0);
		else
			timer_event(a, TE_QUEUE);
		z->accepting_mouse = 0;
		break;
	case GS_PROCESS_MOVE: /* 1188:8852 */
		if (z->zazu_looking && !behind_rock(z)) {
			game_event(a, GE_LOSE, 0);
		} else if (in_success_position(z)) {
			game_event(a, GE_WIN, 0);
		} else {
			int mx, my;
			rt_mouse_pos(&mx, &my); /* vtable +0x50 */
			process_move(a, mx, my);
			if (z->sound_enabled)
				sound_play(z->sound1);
			timer_event(a, TE_QUEUE);
		}
		break;
	case GS_DEFEAT: /* 1188:88ee */
		trace("defeat");
		rt_queue_actor_event(a->id, EV_MINIGAME_DEFEAT, NULL);
		break;
	case GS_SUCCESS: /* 1188:8928 */
		trace("success");
		rt_queue_actor_event(a->id, EV_MINIGAME_SUCCESS, NULL);
		break;
	}
}

/* state entry handlers of the timer machine (DS:016E, filled by 1188:8ad4) */
static void timer_enter(MsActor *a, int state) {
	switch (state) {
	case TS_EXPIRED: /* 1188:8a12 */
		game_event(a, GE_MOVE, 0);
		break;
	case TS_QUEUED: /* 1188:8a30: the timer fires after one period */
		actor_start_alarm(a, (uint32_t)(Z(a)->period_s * 1000));
		break;
	default: /* 1188:8a50, 8a54, 8a58: nothing */
		break;
	}
}

/* ---- actor interface ---- */

/* 1188:7398 */
static void set_active(MsActor *a, int active) {
	Zazu *z = Z(a);
	if (z->active == active)
		return;
	z->active = active;
	if (!active) {
		z->mouse_inside = 0; /* DS:0166 */
		z->mouse_down = 0;   /* DS:016A */
	}
	timer_event(a, active ? TE_ACTIVATE : TE_INACTIVATE);
	rt_invalidate_mouse();
}

/* 1188:7cc2: exe: from the target towards Simba (Simba east of the target = east) */
static int look_direction(Zazu *z) {
	double ang = angle_of(z->simba_pos.x - z->target.x, z->simba_pos.y - z->target.y);
	if (-180.0 <= ang && ang < -90.0)
		return D_WEST;
	if (-90.0 <= ang && ang < 90.0)
		return D_EAST;
	if (90.0 <= ang && ang < 180.0)
		return D_WEST;
	return D_NONE;
}

/* vtable +4 (1188:70f2, jump table 1188:71b5) */
static MsValue zazu_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Zazu *z = Z(a);
	switch (m) {
	case M_ZAZU_START_LOOKING: /* 0xdf */
		set_zazu_look(a, 1);
		return val_empty();
	case M_ZAZU_STOP_LOOKING: /* 0xe0 */
		set_zazu_look(a, 0);
		return val_empty();
	case M_ZAZU_GET_LOOK_DIRECTION: /* 0xe1: west token only for west, else east */
		return val_token(look_direction(z) == D_WEST ? 0x44c : 0x450);
	case M_MINIGAME_RESET: { /* 0xe2: _ftol of the argument (0 if not a float), no count check */
		double f = nargs >= 1 && args[0].type == VAL_FLOAT ? args[0].u.d : 0.0;
		game_event(a, GE_RESET, (int16_t)(int32_t)f);
		return val_empty();
	}
	case M_MINIGAME_ACTIVATE: /* 0xe3 */
		set_active(a, 1);
		return val_empty();
	case M_MINIGAME_DEACTIVATE: /* 0xe4 */
		set_active(a, 0);
		return val_empty();
	case 0xe7: /* exe: 0xe7 disables the audio (1188:72d8 with 0) and stops both sounds */
		z->sound_enabled = 0;
		sound_stop(z->sound1);
		sound_stop(z->sound2);
		return val_empty();
	case 0xe8: /* exe: 0xe8 enables it */
		z->sound_enabled = 1;
		return val_empty();
	}
	*handled = 0; /* 1190:93a4 */
	return val_empty();
}

/* vtable +0x18 (1188:722a) */
static void zazu_load_complete(MsActor *a) {
	Zazu *z = Z(a);
	if (z->field_size.x == 0 || z->field_size.y == 0) {
		/* bounding box given after the field parameter: the exe would divide by 0 */
		z->field_size.x = a->box.w;
		z->field_size.y = a->box.h;
		z->target.x = (int16_t)(a->box.w / 2);
	}
	z->bounds.x = 0;
	z->bounds.y = 0;
	z->bounds.w = a->box.w;
	z->bounds.h = a->box.h;
	MsActor *s = god_actor(z->simba);
	if (s && s->type == ACTOR_SPRITE) { /* inset by half of Simba's width (1190:7088) */
		int half = (int)(int32_t)(actor_bbox(s).w * 0.5);
		if (z->bounds.w - 2 * half > 0) {
			z->bounds.x = (int16_t)(z->bounds.x + half);
			z->bounds.w = (int16_t)(z->bounds.w - 2 * half);
		} else {
			z->bounds.x = (int16_t)(z->bounds.x + z->bounds.w / 2);
			z->bounds.w = 0;
		}
	}
	trace("field %ux%u, %d rocks, target %d,%d, bounds %d,%d %dx%d", z->rows, z->cols, z->nrocks,
		z->target.x + a->box.x, z->target.y + a->box.y, z->bounds.x, z->bounds.y, z->bounds.w, z->bounds.h);
	if (z->rocks_to_show_given)
		game_event(a, GE_RESET, z->rocks_to_show);
}

/* vtable +0x1c (1188:71ce): the three minigame events run their responses */
static void zazu_event(MsActor *a, int ev, const MsValue *arg) {
	(void)arg;
	if (ev == EV_MINIGAME_SUCCESS || ev == EV_MINIGAME_DEFEAT || ev == EV_MINIGAME_CROUCHED)
		actor_run_response(a, ev, NULL);
	else
		ms_warn("Stalking Zazu %u: no handler for event 0x%x", a->id, ev);
}

/* timer callback (1188:8aac) */
static void zazu_alarm(MsActor *a) {
	timer_event(a, TE_EXPIRE);
}

static void zazu_destroy(MsActor *a) {
	free(Z(a)->field);
}

/* Mouse interface used by the root stage (runtime.c), as vtable +0x2c (1188:7414). */
int zazu_is_active(MsActor *a) {
	return Z(a)->active;
}

/* 1188:74c2: only Simba's box (1190:6c02) while +0x90 is set, everywhere otherwise */
int zazu_mouse_interesting(MsActor *a, int x, int y) {
	Zazu *z = Z(a);
	if (!z->accepting_mouse)
		return 1;
	MsActor *s = god_actor(z->simba);
	if (!s || s->type != ACTOR_SPRITE)
		return 0;
	MsRect b = s->box;
	return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

int zazu_mouse_inside(MsActor *a) {
	return Z(a)->mouse_inside;
}

int zazu_mouse_is_down(MsActor *a) {
	return Z(a)->mouse_down;
}

void zazu_mouse_event(MsActor *a, int ev) {
	Zazu *z = Z(a);
	switch (ev) {
	case EV_MOUSE_DOWN: /* 1188:750e */
		z->mouse_down = 1;
		game_event(a, GE_CROUCH, 0);
		break;
	case EV_MOUSE_UP: /* 1188:7532 */
		z->mouse_down = 0;
		game_event(a, GE_UNCROUCH, 0);
		break;
	case EV_MOUSE_ENTERED: /* 1188:7556 */
		z->mouse_inside = 1;
		if (z->cursor)
			rt_cursor_set_temporary(z->cursor);
		else
			rt_cursor_unset_temporary();
		break;
	case EV_MOUSE_EXITED: /* 1188:7594 */
		z->mouse_inside = 0;
		break;
	}
}

const MsActorType ms_type_zazu = {"StalkingZazu", sizeof(Zazu), 1, zazu_init, zazu_param, NULL,
	zazu_load_complete, zazu_method, zazu_event, zazu_alarm, NULL, zazu_destroy};
