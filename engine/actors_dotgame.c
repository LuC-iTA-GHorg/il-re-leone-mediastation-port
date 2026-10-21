/* "Dot game" (actor type 0x10): the constellations minigame of The Lion King, where the player
 * joins the stars. Checked method by method against LIONKING.EXE (class at 1190:b236, vtable
 * 1190:beb6, object size 0x4c); ScummVM's engines/mediastation/minigames/dotgame.cpp was the
 * starting point.
 *
 * Fields of the original object: +0x30 number of dots, +0x32 dot array, +0x36 start hotspot,
 * +0x38 end sprite, +0x3a marker sprite, +0x3c current dot, +0x3e active (also the "visible"
 * answer of vtable +0x20, 1190:b518), +0x40 speed, +0x42 marker position, +0x46 progress,
 * +0x48 line thickness, +0x49..+0x4b colour. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script.h"

typedef struct {
	uint16_t total;
	MsPoint *dots;
	uint16_t start_hotspot, end_hotspot, marker;
	uint16_t current;
	uint16_t speed;
	uint8_t thickness; /* read but never used by the exe (see line()) */
	uint8_t r, g, b;   /* idem */
	MsPoint pos;
	uint16_t progress;
} Dot;

#define D(a) ((Dot *)(a)->x)

/* constructor 1190:b236 */
static void dot_init(MsActor *a) {
	Dot *d = D(a);
	d->speed = 1;
	d->progress = 100;
	d->thickness = 3;
	d->r = d->g = d->b = 0xff;
}

/* 1190:b328 */
static int dot_param(MsActor *a, MsChunk *c, uint16_t p) {
	Dot *d = D(a);
	switch (p) {
	case 0x001d: /* number of dots, then the dot positions */
		d->total = ms_t_u16(c);
		d->dots = realloc(d->dots, sizeof(MsPoint) * (size_t)(d->total ? d->total : 1));
		for (int i = 0; i < d->total; i++)
			d->dots[i] = ms_t_point(c);
		return 1;
	case AH_EDITABLE:
		a->visible = ms_t_u8(c) != 0;
		return 1;
	case AH_DOT_HELPER_SPRITE1:
		d->start_hotspot = ms_t_u16(c);
		return 1;
	case AH_DOT_HELPER_SPRITE2:
		d->end_hotspot = ms_t_u16(c);
		return 1;
	case AH_DOT_STATE:
		d->marker = ms_t_u16(c);
		return 1;
	case AH_DOT_SPEED:
		d->speed = ms_t_u16(c);
		return 1;
	case AH_DOT_LINE_THICKNESS:
		d->thickness = ms_t_u8(c);
		return 1;
	case AH_DOT_COLOR:
		d->r = ms_t_u8(c);
		d->g = ms_t_u8(c);
		d->b = ms_t_u8(c);
		return 1;
	}
	return 0;
}

static void call0(uint16_t id, uint32_t m) {
	MsActor *t = god_actor(id);
	if (t) {
		MsValue r = actor_call_method(t, m, NULL, 0);
		val_free(&r);
	}
}

static void call_xy(uint16_t id, uint32_t m, int x, int y) {
	MsActor *t = god_actor(id);
	if (t) {
		MsValue args[2] = {val_float(x), val_float(y)};
		MsValue r = actor_call_method(t, m, args, 2);
		val_free(&r);
	}
}

/* 1190:b944: mouseActivate on the start hotspot, show + play on the end sprite */
static void activate_helpers(MsActor *a) {
	Dot *d = D(a);
	call0(d->start_hotspot, M_MOUSE_ACTIVATE);
	call0(d->end_hotspot, M_SPATIAL_SHOW);
	call0(d->end_hotspot, M_TIME_PLAY);
}

/* 1190:ba1e */
static void deactivate_helpers(MsActor *a) {
	Dot *d = D(a);
	call0(d->start_hotspot, M_MOUSE_DEACTIVATE);
	call0(d->end_hotspot, M_SPATIAL_HIDE);
	call0(d->end_hotspot, M_TIME_STOP);
}

/* 1190:b794: both helpers centred on the next dot (dot 0 after the last one). No check of
 * the active flag: every caller has it set. */
static void update_helpers(MsActor *a) {
	Dot *d = D(a);
	if (d->total == 0)
		return; /* the exe would read past the array; never in the data */
	int next = d->total - d->current == 1 ? 0 : d->current + 1;
	if (next >= d->total)
		next = 0; /* idem (current >= total cannot happen, see do_reset/do_hit) */
	MsPoint p = d->dots[next];
	int x = p.x + a->box.x, y = p.y + a->box.y;
	call_xy(d->start_hotspot, M_SPATIAL_CENTER_MOVE_TO, x, y);
	call_xy(d->end_hotspot, M_SPATIAL_CENTER_MOVE_TO, x, y);
}

/* method 0xe3 (1190:b6f6) */
static void activate(MsActor *a) {
	if (a->visible)
		return;
	a->visible = 1;
	if (script_trace) /* test aid: which figure is in play */
		fprintf(stderr, "[%6u] dot game %u activated, dot %u of %u\n", rt_now(), a->id, D(a)->current, D(a)->total);
	activate_helpers(a);
	update_helpers(a);
	actor_invalidate(a); /* vtable +0x54 */
}

/* method 0xe4 (1190:b6f6) */
static void deactivate(MsActor *a) {
	if (!a->visible)
		return;
	a->visible = 0;
	deactivate_helpers(a);
	actor_invalidate(a);
}

/* method 0xe2 (1190:be36): only with exactly one argument; _ftol of the number, ignored when
 * negative or >= the number of dots (no clamping) */
static void do_reset(MsActor *a, MsValue *arg) {
	Dot *d = D(a);
	double f = arg->type == VAL_FLOAT ? arg->u.d : 0.0; /* 1190:4dc0: 0.0 if not a float */
	int n = (int16_t)(int32_t)f;
	if (n < 0 || n >= d->total)
		return;
	d->current = (uint16_t)n;
	if (a->visible) {
		update_helpers(a);
		actor_invalidate(a);
	}
}

/* method 0xe5 (1190:baf8): the marker runs from the current dot to the next one, redrawing
 * the screen at every step, then either the figure is complete or the helpers move on. */
static void do_hit(MsActor *a) {
	Dot *d = D(a);
	if (!a->visible || d->total == 0)
		return;
	d->current++;
	call0(d->marker, M_SPATIAL_SHOW);
	deactivate_helpers(a);
	d->progress = 0;
	while (d->progress <= 99) {
		/* speed 0 would loop forever in the exe; not in the data */
		d->progress = (uint16_t)(d->progress + (d->speed ? d->speed : 1));
		MsPoint s = d->dots[d->current - 1];
		MsPoint e = d->current == d->total ? d->dots[0] : d->dots[d->current];
		/* 32-bit imul/idiv: truncated towards zero */
		d->pos.x = (int16_t)(s.x + (int32_t)d->progress * (e.x - s.x) / 100);
		d->pos.y = (int16_t)(s.y + (int32_t)d->progress * (e.y - s.y) / 100);
		MsActor *marker = god_actor(d->marker);
		if (marker) {
			call_xy(d->marker, M_SPATIAL_CENTER_MOVE_TO, a->box.x + d->pos.x, a->box.y + d->pos.y);
			MsValue t = val_bool(1);
			MsValue r = actor_call_method(marker, M_INCREMENT_FRAME, &t, 1);
			val_free(&r);
		}
		actor_invalidate(a);
		rt_perform_update_dirty(); /* 1190:8e76 */
	}
	d->progress = 100;
	call0(d->marker, M_SPATIAL_HIDE);
	actor_invalidate(a);
	if (d->total - d->current == 1) {
		deactivate(a); /* its own method 0xe4 */
		d->current = 0;
		d->progress = 100;
		rt_trace_event(a, EV_MINIGAME_SUCCESS, NULL);
		actor_run_response(a, EV_MINIGAME_SUCCESS, NULL); /* 1190:3766 */
	} else {
		update_helpers(a);
		activate_helpers(a);
	}
}

/* vtable +4 (1190:b6f6) */
static MsValue dot_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	switch (m) {
	case M_MINIGAME_RESET:
		if (nargs == 1)
			do_reset(a, &args[0]);
		return val_empty();
	case M_MINIGAME_ACTIVATE:
		activate(a);
		return val_empty();
	case M_MINIGAME_DEACTIVATE:
		deactivate(a);
		return val_empty();
	case M_DOT_GAME_HIT:
		do_hit(a);
		return val_empty();
	}
	*handled = 0;
	return val_empty();
}

/* 1190:b4c8: an actor loaded as active clears the flag and calls its own method 0xe4, which
 * then does nothing (the flag is already 0): it ends up inactive without touching the
 * helpers. */
static void dot_load_complete(MsActor *a) {
	a->visible = 0;
}

/* 1188:43d6: the exe ignores the thickness and the colour of the actor (they are pushed but
 * never read): every line is drawn with palette index 1, two pixels wide, straight into the
 * back buffer without clipping. Bresenham from the left end point (x0 <= x1; for a vertical
 * line the second point given); each step also sets the pixel on the left (steep lines) or
 * the one above (shallow lines). A vertical line covers |dy| pixels from its start point,
 * the end point excluded; a single point draws nothing. */
static void plot2(int x, int y, int x2, int y2) {
	rt_draw_thick_line(x, y, x, y, 1, 1);
	rt_draw_thick_line(x2, y2, x2, y2, 1, 1);
}

static void line(MsActor *a, MsPoint p, MsPoint q) {
	int x1 = a->box.x + p.x, y1 = a->box.y + p.y, x2 = a->box.x + q.x, y2 = a->box.y + q.y;
	int lx, ly, rx, ry;
	if (x1 < x2) {
		lx = x1, ly = y1, rx = x2, ry = y2;
	} else {
		lx = x2, ly = y2, rx = x1, ry = y1;
	}
	int dx = rx - lx, dy = ry - ly;
	if (dx == 0) {
		int step = dy < 0 ? -1 : 1, n = dy < 0 ? -dy : dy;
		for (int i = 0, y = ly; i < n; i++, y += step)
			plot2(lx, y, lx - 1, y);
		return;
	}
	int x = lx, y = ly;
	int *major, *minor, smaj, smin, incE, incNE, err, count, ox, oy;
	if (dy <= 0) {
		int ady = -dy;
		if (dx < ady) { /* steep, going up */
			incE = 2 * dx;
			err = 2 * dx + dy;
			incNE = 2 * (dx + dy);
			major = &y, smaj = -1, minor = &x, smin = 1;
			ox = -1, oy = 0, count = ady;
		} else { /* shallow, going up */
			incE = 2 * ady;
			err = 2 * ady - dx;
			incNE = 2 * (ady - dx);
			major = &x, smaj = 1, minor = &y, smin = -1;
			ox = 0, oy = -1, count = dx;
		}
	} else if (dx < dy) { /* steep, going down */
		incE = 2 * dx;
		err = 2 * dx - dy;
		incNE = 2 * (dx - dy);
		major = &y, smaj = 1, minor = &x, smin = 1;
		ox = -1, oy = 0, count = dy;
	} else { /* shallow, going down */
		incE = 2 * dy;
		err = 2 * dy - dx;
		incNE = 2 * (dy - dx);
		major = &x, smaj = 1, minor = &y, smin = 1;
		ox = 0, oy = -1, count = dx;
	}
	plot2(x, y, x + ox, y + oy);
	for (int i = 0; i < count; i++) {
		*major += smaj;
		if (err >= 0) {
			*minor += smin;
			err += incNE;
		} else {
			err += incE;
		}
		plot2(x, y, x + ox, y + oy);
	}
}

/* vtable +0x28 (1190:b526): the finished segments, then the one being drawn */
static void dot_draw(MsActor *a) {
	Dot *d = D(a);
	if (!a->visible || d->current == 0 || d->total == 0)
		return;
	for (int i = 0; i < d->current - 1; i++)
		line(a, d->dots[i], d->dots[i + 1]);
	MsPoint last = d->dots[d->current - 1];
	if (d->progress < 100)
		line(a, last, d->pos);
	else if (d->total != d->current)
		line(a, last, d->dots[d->current]);
	else
		line(a, last, d->dots[0]);
}

static void dot_destroy(MsActor *a) {
	free(D(a)->dots);
}

const MsActorType ms_type_dotgame = {"DotGame", sizeof(Dot), 1, dot_init, dot_param, NULL,
	dot_load_complete, dot_method, NULL, NULL, dot_draw, dot_destroy};
