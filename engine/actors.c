/* The actor types used by first-generation titles (The Lion King). Behaviour follows the
 * original player as reconstructed by ScummVM (engines/mediastation/actors/). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script.h"

#define X(type, a) ((type *)(a)->x)

static void register_main_channel(MsActor *a, MsChunk *c) {
	a->channel = ms_t_channel(c);
	god_register_channel(a->channel, a, CH_MAIN);
}

/* ===================================================================== screen */

typedef struct {
	uint16_t cursor;
} Screen;

static int screen_param(MsActor *a, MsChunk *c, uint16_t p) {
	if (p == AH_CURSOR_RESOURCE_ID) {
		X(Screen, a)->cursor = ms_t_u16(c);
		if (X(Screen, a)->cursor)
			rt_cursor_register_permanent(X(Screen, a)->cursor);
		return 1;
	}
	return 0;
}

static void screen_event(MsActor *a, int ev, const MsValue *arg) {
	switch (ev) {
	case EV_SCREEN_ENTRY:
	case EV_SCREEN_EXIT:
		actor_run_response(a, ev, NULL);
		break;
	case EV_CONTEXT_LOAD_COMPLETE:
	case EV_CONTEXT_ALREADY_LOADED:
		if (ms_god.contexts[val_as_actor(arg)])
			actor_run_response(a, ev, arg);
		break;
	case EV_CONTEXT_RELEASE_COMPLETE:
	case EV_CONTEXT_ALREADY_RELEASED:
		if (!ms_god.contexts[val_as_actor(arg)])
			actor_run_response(a, ev, arg);
		break;
	default:
		ms_warn("screen %u: no handler for event 0x%x", a->id, ev);
	}
}

const MsActorType ms_type_screen = {"Screen", sizeof(Screen), 0, NULL, screen_param, NULL, NULL,
	NULL, screen_event, NULL, NULL, NULL};

/* =================================================================== document */

/* The document actor (LIONKING.EXE 1198:7f92, constructor 1198:8038) has two methods:
 * - branchToScreen (0xc9, 1198:7ec4): nothing without an argument or when the first one is
 *   not an actor; otherwise event ScreenBranch (0x12) posted to the document with the screen
 *   and "disable auto update" (1, or 0 when the second argument is token 0x708);
 * - quit (0xd9): the event loop stops after the current pass (1188:fd04, loop +0x3e).
 * Every other method goes to the actor base (1190:27f8) and does nothing: this engine
 * version has no loadContext/releaseContext/contextIsLoaded/isLoading/multiple sounds or
 * streams (later engines, kept by ScummVM). */
static MsValue document_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	(void)a;
	(void)handled;
	switch (m) {
	case M_DOCUMENT_BRANCH_TO_SCREEN: {
		if (nargs < 1 || args[0].type != VAL_ACTOR || val_as_actor(&args[0]) == 0)
			break;
		int disable_auto_update = 1;
		if (nargs > 1 && args[1].type == VAL_TOKEN && val_as_token(&args[1]) == 0x708)
			disable_auto_update = 0;
		rt_queue_branch((uint16_t)val_as_actor(&args[0]), disable_auto_update);
		break;
	}
	case M_DOCUMENT_QUIT:
		rt_quit();
		break;
	}
	return val_empty();
}

/* the document events (screen branch) are handled by the runtime */
const MsActorType ms_type_document = {"Document", 0, 0, NULL, NULL, NULL, NULL, document_method,
	NULL, NULL, NULL, NULL};

/* ==================================================================== palette */

typedef struct {
	uint8_t pal[768];
	int have;
} Palette;

static int palette_param(MsActor *a, MsChunk *c, uint16_t p) {
	if (p == AH_PALETTE) {
		ms_read(c, X(Palette, a)->pal, 768);
		X(Palette, a)->have = 1;
		return 1;
	}
	return 0;
}

const uint8_t *palette_actor_colors(MsActor *a) {
	if (!a || a->type != ACTOR_PALETTE || !X(Palette, a)->have)
		return NULL;
	return X(Palette, a)->pal;
}

const MsActorType ms_type_palette = {"Palette", sizeof(Palette), 0, NULL, palette_param, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL};

/* ===================================================================== cursor */

typedef struct {
	uint16_t cursor;
} Cursor;

static int cursor_param(MsActor *a, MsChunk *c, uint16_t p) {
	if (p == AH_CURSOR_RESOURCE_ID) {
		X(Cursor, a)->cursor = ms_t_u16(c);
		return 1;
	}
	return 0;
}

static MsValue cursor_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	(void)args;
	(void)nargs;
	/* LIONKING.EXE 1190:b1a0: method 0xc8 makes the cursor (parameter 0x22, 1190:b16c)
	 * permanent if it is not 0 (1190:67be); every other method: actor base, nothing */
	(void)handled;
	if (m == M_CURSOR_SET && X(Cursor, a)->cursor)
		rt_cursor_set_permanent(X(Cursor, a)->cursor);
	return val_empty();
}

const MsActorType ms_type_cursor = {"Cursor", sizeof(Cursor), 0, NULL, cursor_param, NULL, NULL,
	cursor_method, NULL, NULL, NULL, NULL};

/* ====================================================================== timer */

typedef struct {
	uint32_t pause_start;
} Timer;

static void timer_stop(MsActor *a) {
	actor_stop_alarm(a);
	X(Timer, a)->pause_start = 0;
	a->start_time = 0;
}

static void timer_start(MsActor *a) {
	timer_stop(a);
	a->start_time = rt_now();
	if (a->start_time == 0)
		a->start_time = 1; /* 0 means "not running" */
	a->last_processed = 0;
	actor_setup_next_time_timer(a);
}

/* Methods of the Timer class (LIONKING.EXE 1190:d82e, vtable 1190:de2c +4): timePlay
 * (1190:d88a: stop, start = now, schedule the first time response, stop if there is none),
 * timeStop (1190:da9c), timePause (1190:db40: only when started and not paused; the timer
 * entry is removed), timeResume (1190:dbd4: only when paused; start moves on by the pause, the
 * next response is scheduled, no arguments); every other method goes to the actor base
 * (1190:27f8) and does nothing (no isPlaying in this engine). */
static MsValue timer_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Timer *t = X(Timer, a);
	(void)args;
	(void)nargs;
	(void)handled;
	switch (m) {
	case M_TIME_PLAY:
		timer_start(a);
		break;
	case M_TIME_STOP:
		timer_stop(a);
		break;
	case M_TIME_PAUSE:
		if (a->start_time > 0 && t->pause_start == 0) {
			t->pause_start = rt_now();
			if (t->pause_start == 0)
				t->pause_start = 1;
			actor_stop_alarm(a);
		}
		break;
	case M_TIME_RESUME:
		if (a->start_time > 0 && t->pause_start > 0) {
			a->start_time += rt_now() - t->pause_start;
			t->pause_start = 0;
			actor_setup_next_time_timer(a);
		}
		break;
	}
	return val_empty();
}

static void timer_alarm(MsActor *a) {
	MsResponse *r = actor_next_time_response(a, a->last_processed);
	if (!r) {
		ms_warn("timer %u: no time response after %u", a->id, a->last_processed);
		return;
	}
	a->last_processed = (uint32_t)(val_as_float(&r->arg) * 1000) + 1;
	/* schedule the next response first: the current one may restart the timer */
	actor_stop_alarm(a);
	if (!actor_setup_next_time_timer(a))
		timer_stop(a);
	uint16_t id = a->id;
	MsValue ret = rt_run_code(id, r->code, r->size, NULL, 0);
	val_free(&ret);
}

const MsActorType ms_type_timer = {"Timer", sizeof(Timer), 0, NULL, NULL, NULL, NULL,
	timer_method, NULL, timer_alarm, NULL, NULL};

/* ======================================================================= path */

/* Path (LIONKING.EXE class 4, constructor 10d8:0000, vtable 11a0:0274). The Lion King has no
 * path actors; this follows the exe exactly. Fields: start point +0x20, end point +0x24,
 * total steps +0x28, step rate +0x2a, current point +0x32, current step +0x36, timer +0x38,
 * period +0x3c, next step time +0x44. */
typedef struct {
	MsPoint start_pt, end_pt, cur;
	uint16_t total_steps, current_step;
	double step_rate;
	double period_s, next_s; /* seconds of the engine clock */
} Path;

/* parameters (10d8:0422): 0x60e start point, 0x60f end point, 0x610 total steps, 0x611 step
 * rate; anything else goes to the actor base */
static int path_param(MsActor *a, MsChunk *c, uint16_t p) {
	Path *t = X(Path, a);
	switch (p) {
	case AH_START_POINT:
		t->start_pt = ms_t_point(c);
		return 1;
	case AH_END_POINT:
		t->end_pt = ms_t_point(c);
		return 1;
	case AH_PATH_TOTAL_STEPS:
		t->total_steps = ms_t_u16(c);
		return 1;
	case AH_STEP_RATE:
		t->step_rate = ms_t_double(c);
		return 1;
	}
	return 0;
}

/* 10d8:0738: next += period; the timer fires at next (now if already past, and next = now) */
static void path_schedule(MsActor *a) {
	Path *t = X(Path, a);
	double now = rt_now() / 1000.0;
	t->next_s += t->period_s;
	double delay = t->next_s - now;
	if (delay < 0.0) {
		delay = 0.0;
		t->next_s = now;
	}
	if (!(delay < 1e6)) /* step rate 0: the period is infinite (never fires) */
		delay = 1e6;
	actor_start_alarm(a, (uint32_t)(delay * 1000.0));
}

/* Methods (10d8:0260): timePlay (10d8:0520: current point = start, step 0, period =
 * 1 / step rate, next = now, then schedule), timeStop (10d8:0812: only the timer is removed,
 * no event), 0xe9 / 0xea current x / y, 0xf2 setStartPoint(x, y), 0xf3 setEndPoint(x, y),
 * 0xf4 setTotalSteps (truncated), 0xf5 setStepRate; the arguments are read without checking
 * their number. Everything else (pause, resume, duration, percent...) goes to the actor base
 * and does nothing. */
static MsValue path_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Path *t = X(Path, a);
	(void)handled;
	MsValue none = val_empty();
	MsValue *a0 = nargs > 0 ? &args[0] : &none, *a1 = nargs > 1 ? &args[1] : &none;
	switch (m) {
	case M_TIME_PLAY:
		t->cur = t->start_pt;
		t->current_step = 0;
		t->period_s = 1.0 / t->step_rate;
		t->next_s = rt_now() / 1000.0;
		path_schedule(a);
		break;
	case M_TIME_STOP:
		actor_stop_alarm(a);
		break;
	case M_GET_LEFT_X:
		return val_float(t->cur.x);
	case M_GET_TOP_Y:
		return val_float(t->cur.y);
	/* 10d8:0260: no argument count check; float() (0.0 if not a float) then _ftol, the step
	 * rate stays a double */
	case M_PATH_SET_START_POINT:
		t->start_pt.x = (int16_t)ms_ftol(val_float_of(a0));
		t->start_pt.y = (int16_t)ms_ftol(val_float_of(a1));
		break;
	case M_PATH_SET_END_POINT:
		t->end_pt.x = (int16_t)ms_ftol(val_float_of(a0));
		t->end_pt.y = (int16_t)ms_ftol(val_float_of(a1));
		break;
	case M_PATH_SET_TOTAL_STEPS:
		t->total_steps = (uint16_t)ms_ftol(val_float_of(a0));
		break;
	case M_PATH_SET_STEP_RATE:
		t->step_rate = val_float_of(a0);
		break;
	}
	return val_empty();
}

/* events (10d8:04b6): PathEnd (0x10) and PathStep (0x1c) run their response */
static void path_event(MsActor *a, int ev, const MsValue *arg) {
	(void)arg;
	if (ev == EV_PATH_END || ev == EV_PATH_STEP)
		actor_run_response(a, ev, NULL);
}

/* timer (10d8:06b8): step++; before the last step the point moves to
 * start + step * (end - start) / total + 0.5, truncated (10d8:05ae), PathStep is posted and
 * the next step scheduled; at the last step the timer is removed and PathEnd posted (the
 * point stays at the previous step) */
static void path_alarm(MsActor *a) {
	Path *t = X(Path, a);
	t->current_step++;
	if (t->current_step < t->total_steps) {
		t->cur.x = (int16_t)((double)t->current_step * ((double)t->end_pt.x - t->start_pt.x) /
					     t->total_steps + t->start_pt.x + 0.5);
		t->cur.y = (int16_t)(((double)t->end_pt.y - t->start_pt.y) * t->current_step /
					     t->total_steps + t->start_pt.y + 0.5);
		rt_queue_actor_event(a->id, EV_PATH_STEP, NULL);
		path_schedule(a);
	} else {
		actor_stop_alarm(a);
		rt_queue_actor_event(a->id, EV_PATH_END, NULL);
	}
}

const MsActorType ms_type_path = {"Path", sizeof(Path), 0, NULL, path_param, NULL, NULL,
	path_method, path_event, path_alarm, NULL, NULL};

/* ==================================================================== hotspot */

typedef struct {
	int active;
	int offstage_events;
	uint16_t cursor;
	MsPoint *poly;
	int npoly;
} Hotspot;

static int hotspot_param(MsActor *a, MsChunk *c, uint16_t p) {
	Hotspot *h = X(Hotspot, a);
	switch (p) {
	case AH_MOUSE_ACTIVE_AREA: {
		uint16_t n = ms_t_u16(c);
		h->poly = realloc(h->poly, sizeof(MsPoint) * (size_t)(n ? n : 1));
		h->npoly = n;
		for (int i = 0; i < n; i++)
			h->poly[i] = ms_t_point(c);
		return 1;
	}
	case AH_STARTUP:
		h->active = ms_t_u8(c) != 0;
		return 1;
	case AH_CURSOR_RESOURCE_ID:
		h->cursor = ms_t_u16(c);
		return 1;
	case AH_GET_OFFSTAGE_EVENTS:
		h->offstage_events = ms_t_u8(c) != 0;
		return 1;
	}
	return 0;
}

/* Point in polygon, exactly as the original (the polygon is relative to the bounding box). */
static int poly_contains(const MsPoint *poly, int n, int px, int py) {
	if (n == 0)
		return 1;
	int rcross = 0;
	for (int i = 0; i < n; i++) {
		MsPoint s = poly[i], e = poly[(i + 1) % n];
		if (s.x == px && s.y == py)
			return 1;
		if ((s.y > py) != (e.y > py)) {
			int t1 = (s.x - px) * (e.y - py) - (e.x - px) * (s.y - py);
			int t2 = (e.y - py) - (s.y - e.y);
			if ((t1 > 0) == (t2 >= 0))
				rcross++;
		}
	}
	return (rcross % 2) == 1;
}

int hotspot_in_bounds(MsActor *a, int x, int y) {
	Hotspot *h = X(Hotspot, a);
	MsRect b = a->box;
	if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h)
		return 0;
	return poly_contains(h->poly, h->npoly, x - b.x, y - b.y);
}

int hotspot_is_active(MsActor *a) {
	return a->type == ACTOR_HOTSPOT && X(Hotspot, a)->active;
}

uint16_t hotspot_cursor(MsActor *a) {
	return X(Hotspot, a)->cursor;
}

static MsValue hotspot_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Hotspot *h = X(Hotspot, a);
	switch (m) {
	case M_MOUSE_ACTIVATE:
		if (!h->active) {
			h->active = 1;
			rt_invalidate_mouse();
		}
		return val_empty();
	case M_MOUSE_DEACTIVATE:
		if (h->active) {
			h->active = 0;
			if (rt_mouse_down_hotspot() == a)
				rt_set_mouse_down_hotspot(NULL);
			if (rt_mouse_inside_hotspot() == a)
				rt_set_mouse_inside_hotspot(NULL);
			rt_invalidate_mouse();
		}
		return val_empty();
	case M_IS_POINT_INSIDE: {
		/* 1190:c126 -> 1190:c00c: rectangle, then the polygon; no argument count check (a
		 * missing argument is the extra empty value: 0), float() then _ftol */
		int x = nargs > 0 ? (int16_t)ms_ftol(val_float_of(&args[0])) : 0;
		int y = nargs > 1 ? (int16_t)ms_ftol(val_float_of(&args[1])) : 0;
		return val_bool(hotspot_in_bounds(a, x, y));
	}
	}
	/* 1190:c126 handles only 0xd2, 0xd3 and 0xf6, the rest goes to 1190:93a4; the methods
	 * below are from later engines */
	if (!script_extensions) {
		*handled = 0;
		return val_empty();
	}
	switch (m) {
	case M_TRIGGER_ABS_X: {
		int x, y;
		rt_mouse_pos(&x, &y);
		return val_float(x);
	}
	case M_TRIGGER_ABS_Y: {
		int x, y;
		rt_mouse_pos(&x, &y);
		return val_float(y);
	}
	case M_IS_ACTIVE:
		return val_bool(h->active);
	}
	*handled = 0;
	return val_empty();
}

static void hotspot_destroy(MsActor *a) {
	free(X(Hotspot, a)->poly);
}

const MsActorType ms_type_hotspot = {"Hotspot", sizeof(Hotspot), 1, NULL, hotspot_param, NULL,
	NULL, hotspot_method, NULL, NULL, NULL, hotspot_destroy};

/* ====================================================================== image */

typedef struct {
	MsBitmap *bmp;
	int shared; /* bitmap owned by another image (actor reference) */
	int decompress_in_place;
} Image;

/* Parameters of the image class (LIONKING.EXE 1190:c7a4): 0x1b channel, 0x1f visible, 0x20
 * (kept, passed to the blit but unused), 0x32 load type, 0x5dc dissolve factor; the rest goes
 * to the spatial base (0x19 stage, 0x1c box, 0x1e z, 0x17 responses) and anything else is the
 * fatal "Attempt to read unrecognized parameter" (1190:272e). The x/y offset (0x5de, 0x5df)
 * and the image reference (0x77b) of later engines are not image parameters here. */

static int image_param(MsActor *a, MsChunk *c, uint16_t p) {
	Image *im = X(Image, a);
	switch (p) {
	case AH_CHANNEL_IDENT:
		register_main_channel(a, c);
		return 1;
	case AH_STARTUP:
		a->visible = ms_t_u8(c) != 0;
		return 1;
	case AH_LOAD_TYPE:
		im->decompress_in_place = ms_t_u8(c);
		return 1;
	}
	return 0;
}

static void image_chunk(MsActor *a, int sub, MsChunk *c) {
	(void)sub;
	Image *im = X(Image, a);
	MsImageInfo info = bitmap_read_info(c);
	if (im->bmp && !im->shared)
		bitmap_free(im->bmp);
	im->bmp = bitmap_read(c, &info, im->decompress_in_place);
	im->shared = 0;
}

/* The image is drawn at the corner of its box (+0x1c, 1190:c96a) and the box itself is what
 * the stage tests and invalidates, whatever the size of the bitmap. */
MsRect image_bbox(MsActor *a) {
	return a->box;
}

MsBitmap *image_bitmap(MsActor *a) {
	return a->type == ACTOR_IMAGE ? X(Image, a)->bmp : NULL;
}

static MsValue image_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	(void)args;
	(void)nargs;
	switch (m) {
	case M_SPATIAL_SHOW:
	case M_SPATIAL_HIDE: {
		/* the area is invalidated only if the visibility test (1190:c8cc: flag and dissolve
		 * factor not 0) gives a different answer afterwards */
		int before = a->visible && a->dissolve != 0.0;
		a->visible = m == M_SPATIAL_SHOW;
		if ((a->visible && a->dissolve != 0.0) != before)
			actor_invalidate(a);
		return val_empty();
	}
	}
	*handled = 0;
	return val_empty();
}

static void image_draw(MsActor *a) {
	Image *im = X(Image, a);
	if (im->bmp) {
		MsRect b = image_bbox(a);
		rt_draw_bitmap(im->bmp, b.x, b.y, a->dissolve);
	}
}

static void image_destroy(MsActor *a) {
	Image *im = X(Image, a);
	/* a bitmap shared by reference stays with the image that loaded it; since the referencing
	 * image may outlive it, shared bitmaps are never freed here (they are freed by the owner,
	 * and references are only made within the same context) */
	if (!im->shared)
		bitmap_free(im->bmp);
	im->bmp = NULL;
}

const MsActorType ms_type_image = {"Image", sizeof(Image), 1, NULL, image_param, image_chunk, NULL,
	image_method, NULL, NULL, image_draw, image_destroy};

/* ====================================================================== sound */

typedef struct {
	MsAudioSeq seq;
	int from_chunk;
	int stream_read;
	int discard_after_use;
	int state; /* AUDIO_* */
	int abort; /* the next stop is an abort (+0x42 of the original object) */
} Sound;

static void sound_state_cb(MsAudioSeq *s, int state, int reason, void *user);

static void sound_init(MsActor *a) {
	Sound *s = X(Sound, a);
	s->seq.voice = -1;
	s->seq.state = AUDIO_STOPPED;
	s->seq.on_state = sound_state_cb;
	s->seq.user = a;
	s->state = AUDIO_STOPPED;
}

static int sound_param(MsActor *a, MsChunk *c, uint16_t p) {
	Sound *s = X(Sound, a);
	switch (p) {
	case AH_ACTOR_ID: {
		uint16_t dup = ms_t_u16(c);
		if (dup != a->id)
			ms_warn("sound %u: duplicate actor id %u", a->id, dup);
		return 1;
	}
	case AH_CHANNEL_IDENT:
		register_main_channel(a, c);
		return 1;
	case AH_DISCARD_AFTER_USE:
		s->discard_after_use = ms_t_u8(c) != 0;
		return 1;
	case AH_SOUND_INFO:
		audio_seq_read_params(&s->seq, c);
		return 1;
	case AH_CACHING_ENABLED:
	case AH_INSTALL_TYPE:
		ms_t_u8(c);
		return 1;
	}
	return 0;
}

static void sound_chunk(MsActor *a, int sub, MsChunk *c) {
	(void)sub;
	Sound *s = X(Sound, a);
	if (!s->stream_read)
		s->from_chunk = 1;
	audio_seq_add_chunk(&s->seq, c);
}

/* State callback of the audio sequence (LIONKING.EXE 1190:d20a, secondary interface +0x2c of
 * the Sound class): stopped (1) with reason 1 → SoundFailure (0x14), 2 → SoundEnd (0x0e),
 * 3 (stopped) → SoundAbort (0x13) when the stop was an abort, else SoundStopped (0x1d); the
 * device is given back (1188:eee2, device 2). Playing (2) after stopped → SoundBegin (0x1e);
 * playing after paused posts nothing. Paused (3) posts nothing. The abort flag is cleared at
 * every state change. */
static void sound_state_cb(MsAudioSeq *seq, int state, int reason, void *user) {
	(void)seq;
	MsActor *a = user;
	Sound *s = X(Sound, a);
	int abort = s->abort;
	s->abort = 0;
	if (state == AUDIO_STOPPED) {
		s->state = AUDIO_STOPPED;
		actor_stop_alarm(a);
		rt_release_sound_device(a);
		int ev = EV_INVALID;
		switch (reason) {
		case STOP_FAILURE:
			ev = EV_SOUND_FAILURE;
			break;
		case STOP_END:
			ev = EV_SOUND_END;
			break;
		case STOP_SCRIPT:
		case STOP_ABORT:
			ev = abort || reason == STOP_ABORT ? EV_SOUND_ABORT : EV_SOUND_STOPPED;
			break;
		}
		rt_queue_actor_event(a->id, ev, NULL);
	} else if (state == AUDIO_PLAYING) {
		if (s->state == AUDIO_STOPPED)
			rt_queue_actor_event(a->id, EV_SOUND_BEGIN, NULL);
		s->state = AUDIO_PLAYING;
	} else if (state == AUDIO_PAUSED) {
		s->state = AUDIO_PAUSED;
	}
}

/* 1190:d442: only a playing or paused sound stops; abort = 1 when another sound takes the
 * device (1190:d630) or timePlay restarts the sound (1190:d3a6), 0 for timeStop. */
static void sound_stop_ex(MsActor *a, int abort) {
	Sound *s = X(Sound, a);
	if (s->state == AUDIO_PLAYING || s->state == AUDIO_PAUSED) {
		s->abort = abort;
		audio_seq_stop(&s->seq);
		actor_stop_alarm(a);
	}
}

void sound_stop(MsActor *a) {
	sound_stop_ex(a, 0);
}

/* timePlay (LIONKING.EXE 1190:d3a6): a playing or paused sound is first aborted (SoundAbort),
 * then the wave device is requested (device owner DS:0236, 1188:ee8c, device 2: the current
 * owner is asked to give up); a streamed sound also takes the CD stream device (1) and
 * registers with the stream feed (1190:d4d4, d4fc, d572); then the sequence starts
 * (1198:a6fa). */
static void sound_start(MsActor *a) {
	Sound *s = X(Sound, a);
	if (s->state == AUDIO_PLAYING || s->state == AUDIO_PAUSED)
		sound_stop_ex(a, 1);
	if (!rt_grant_sound_device(a)) {
		ms_warn("sound %u: device grant failed", a->id);
		return;
	}
	if (!s->from_chunk && !s->stream_read) {
		s->stream_read = 1;
		god_read_stream(a->id);
	}
	/* the Sound class has no timer (no timer object, 1190:d3a6 starts none): "On Time"
	 * responses of a sound never run (the Lion King has none) */
	audio_seq_start(&s->seq);
}

/* Methods of the Sound class (LIONKING.EXE 1190:d348, vtable 1190:d694 +4): nothing at all
 * before the actor is loaded (+0x1a); timePlay, timeStop, timePause, timeResume; every other
 * method goes to the actor base (1190:27f8), which does nothing. isPlaying/isPaused/show are
 * kept from ScummVM (later engines); the Lion King scripts may reach them through the
 * interpreter. */
static MsValue sound_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Sound *s = X(Sound, a);
	(void)args;
	(void)nargs;
	/* 1190:d348 tests +0x1a before the method number: nothing at all before loading */
	if (!a->loaded && !script_extensions)
		return val_empty();
	switch (m) {
	case M_TIME_PLAY:
		sound_start(a);
		return val_empty();
	case M_TIME_STOP:
		sound_stop(a); /* 1190:d442(0): SoundStopped */
		return val_empty();
	case M_TIME_PAUSE:
		/* 1190:d46c: only while playing; the device stays owned (no 1188:eee2), a streamed
		 * sound pauses its stream feed (1190:d598) */
		if (s->state == AUDIO_PLAYING)
			audio_seq_pause(&s->seq);
		return val_empty();
	case M_TIME_RESUME:
		/* 1190:d4a0: only while paused, no arguments, no new device request */
		if (s->state == AUDIO_PAUSED)
			audio_seq_resume(&s->seq);
		return val_empty();
	}
	if (script_extensions) /* later engines */
		switch (m) {
		case M_SPATIAL_SHOW:
			return val_empty();
		case M_IS_PLAYING:
			return val_bool(s->state == AUDIO_PLAYING || s->state == AUDIO_PAUSED);
		case M_IS_PAUSED:
			return val_bool(s->state == AUDIO_PAUSED);
		}
	*handled = 0;
	return val_empty();
}

/* 1190:d30e (vtable +0x1c): the response of the event, with an empty argument; SoundEnd
 * runs no left-over time responses (that came from ScummVM, later engines) */
static void sound_event(MsActor *a, int ev, const MsValue *arg) {
	(void)arg;
	actor_run_response(a, ev, NULL);
}

static void sound_destroy(MsActor *a) {
	Sound *s = X(Sound, a);
	if (s->state == AUDIO_PLAYING || s->state == AUDIO_PAUSED) {
		s->seq.on_state = NULL;
		audio_seq_stop(&s->seq);
		rt_release_sound_device(a);
	}
	audio_seq_free(&s->seq);
}

/* device owner give-up (1190:d630): stops with abort (SoundAbort) and always agrees */
int sound_device_give_up(MsActor *a) {
	sound_stop_ex(a, 1);
	return 0;
}

const MsActorType ms_type_sound = {"Sound", sizeof(Sound), 0, sound_init, sound_param,
	sound_chunk, NULL, sound_method, sound_event, NULL, NULL, sound_destroy};

/* ===================================================================== sprite */

typedef struct {
	uint16_t id;
	int first, last;
} Clip;

typedef struct {
	int index;
	MsPoint origin;
	MsBitmap *bmp;
} SpriteFrame;

typedef struct {
	SpriteFrame *frames;
	int nframes;
	int frame_count;
	int refs;
} SpriteAsset;

#define CLIP_FORWARD 0x4b0

typedef struct {
	SpriteAsset *asset;
	int decompress_in_place;
	uint32_t frame_rate;
	Clip clips[64];
	int nclips;
	int playing;
	int current;
	uint32_t next_frame_time;
	uint16_t default_clip;
	Clip active;
} Sprite;

static void sprite_init(MsActor *a) {
	X(Sprite, a)->default_clip = CLIP_FORWARD;
}

/* The clip list is searched from the start and the first clip with the id wins
 * (LIONKING.EXE 1068:0bf6); clips are only appended (1088:0726). */
static Clip *sprite_clip(Sprite *s, uint16_t id) {
	for (int i = 0; i < s->nclips; i++)
		if (s->clips[i].id == id)
			return &s->clips[i];
	return NULL;
}

static void sprite_add_clip(Sprite *s, Clip c) {
	if (s->nclips < 64)
		s->clips[s->nclips++] = c;
}

static void sprite_set_clip(MsActor *a, uint16_t id);

static int sprite_param(MsActor *a, MsChunk *c, uint16_t p) {
	Sprite *s = X(Sprite, a);
	switch (p) {
	case AH_CHANNEL_IDENT:
		register_main_channel(a, c);
		s->asset = calloc(1, sizeof(SpriteAsset));
		s->asset->refs = 1;
		return 1;
	case AH_STARTUP:
		a->visible = ms_t_u8(c) != 0;
		return 1;
	case AH_FRAME_RATE:
		s->frame_rate = (uint32_t)ms_t_double(c);
		return 1;
	case AH_LOAD_TYPE:
		s->decompress_in_place = ms_t_u8(c) != 0;
		return 1;
	case AH_SPRITE_CHUNK_COUNT:
		if (!s->asset) {
			s->asset = calloc(1, sizeof(SpriteAsset));
			s->asset->refs = 1;
		}
		s->asset->frame_count = ms_t_u16(c);
		/* The frame count also adds the only built-in clip, 0x4b0 = [0, count - 1] (16-bit),
		 * and makes it current (LIONKING.EXE 1068:02b2 at 1068:0462). There is no built-in
		 * backward clip 0x4b1 in this engine. */
		sprite_add_clip(s, (Clip){CLIP_FORWARD, 0, (uint16_t)(s->asset->frame_count - 1)});
		sprite_set_clip(a, CLIP_FORWARD);
		return 1;
	case AH_SPRITE_CLIP: {
		Clip cl;
		cl.id = ms_t_u16(c);
		cl.first = ms_t_u16(c);
		cl.last = ms_t_u16(c);
		sprite_add_clip(s, cl);
		return 1;
	}
	case AH_DEFAULT_SPRITE_CLIP:
		/* made current at once, while the parameters are read (1068:02b2 -> 1068:0bf6) */
		s->default_clip = ms_t_u16(c);
		sprite_set_clip(a, s->default_clip);
		return 1;
	case AH_ACTOR_REFERENCE: {
		uint16_t ref = ms_t_u16(c);
		MsActor *r = god_actor(ref);
		if (!r || r->type != ACTOR_SPRITE)
			ms_fatal("sprite %u: referenced sprite %u not loaded", a->id, ref);
		if (s->asset && --s->asset->refs == 0)
			free(s->asset);
		s->asset = X(Sprite, r)->asset;
		s->asset->refs++;
		return 1;
	}
	}
	return 0;
}

static int frame_cmp(const void *x, const void *y) {
	return ((const SpriteFrame *)x)->index - ((const SpriteFrame *)y)->index;
}

static void sprite_chunk(MsActor *a, int sub, MsChunk *c) {
	(void)sub;
	Sprite *s = X(Sprite, a);
	MsImageInfo info = bitmap_read_info(c);
	SpriteFrame f;
	f.index = ms_t_u16(c);
	f.origin = ms_t_point(c);
	f.bmp = bitmap_read(c, &info, s->decompress_in_place);
	SpriteAsset *as = s->asset;
	as->frames = realloc(as->frames, sizeof(SpriteFrame) * (size_t)(as->nframes + 1));
	as->frames[as->nframes++] = f;
	qsort(as->frames, (size_t)as->nframes, sizeof(SpriteFrame), frame_cmp);
}

static void sprite_dirty(MsActor *a) {
	if (a->visible)
		actor_invalidate(a);
}

static void sprite_set_initial(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (s->current != s->active.first) {
		sprite_dirty(a);
		s->current = s->active.first;
		sprite_dirty(a);
	}
}

static void sprite_set_final(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (s->current != s->active.last) {
		sprite_dirty(a);
		s->current = s->active.last;
		sprite_dirty(a);
	}
}

/* LIONKING.EXE 1068:0bf6: nothing happens for the current clip id; an unknown id leaves the
 * clip (and its id) as it is, but the current frame still goes back to the clip's first. */
static void sprite_set_clip(MsActor *a, uint16_t id) {
	Sprite *s = X(Sprite, a);
	if (s->active.id == id)
		return;
	Clip *c = sprite_clip(s, id);
	if (c)
		s->active = *c;
	else
		ms_warn("sprite %u: clip 0x%x not found", a->id, id);
	sprite_set_initial(a);
}

/* Frames only step forward inside [first, last] (unsigned 16-bit compares): next needs
 * current < last (1068:0ba2), previous needs first < current (1068:0bcc). A clip with
 * first > last is never stepped and never played (only its first frame is shown). */
static int sprite_next(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if ((uint16_t)s->current < (uint16_t)s->active.last) {
		sprite_dirty(a);
		s->current++;
		sprite_dirty(a);
		return 1;
	}
	return 0;
}

static int sprite_prev(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if ((uint16_t)s->active.first < (uint16_t)s->current) {
		sprite_dirty(a);
		s->current--;
		sprite_dirty(a);
		return 1;
	}
	return 0;
}

/* Stopping only removes the frame timer (LIONKING.EXE 1068:0f6c): the next frame time is kept. */
static void sprite_stop(MsActor *a) {
	Sprite *s = X(Sprite, a);
	s->playing = 0;
	actor_stop_alarm(a);
}

/* LIONKING.EXE 1068:0e82: next frame time += 1 / frame rate (1068:03c4); if that is already in
 * the past it becomes "now", so a late or first frame is shown at once and frames are never
 * skipped. The next frame time starts at 0 and is never reset (not by play nor stop): the
 * first play after an idle period therefore advances a frame immediately. */
static void sprite_schedule_timer(MsActor *a) {
	Sprite *s = X(Sprite, a);
	uint32_t dur = s->frame_rate ? 1000 / s->frame_rate : 100;
	s->next_frame_time += dur;
	uint32_t now = rt_now();
	if (s->next_frame_time < now)
		s->next_frame_time = now;
	actor_start_alarm(a, s->next_frame_time - now);
}

/* LIONKING.EXE 1068:0e52: while playing, the next frame is scheduled only if current < last. */
static void sprite_schedule(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (!s->playing)
		return;
	if ((uint16_t)s->current < (uint16_t)s->active.last)
		sprite_schedule_timer(a);
	else
		sprite_stop(a);
}

static MsValue sprite_method(MsActor *a, uint32_t m, MsValue *args, int nargs, int *handled) {
	Sprite *s = X(Sprite, a);
	switch (m) {
	case M_SPATIAL_SHOW:
	case M_SPATIAL_HIDE: {
		int v = m == M_SPATIAL_SHOW;
		if (a->visible != v) {
			a->visible = v;
			actor_invalidate(a);
		}
		return val_empty();
	}
	case M_TIME_PLAY:
		s->playing = 1;
		a->start_time = rt_now();
		a->last_processed = 0;
		sprite_schedule(a);
		return val_empty();
	case M_TIME_STOP:
		sprite_stop(a);
		return val_empty();
	case M_MOVIE_RESET:
		sprite_set_initial(a);
		return val_empty();
	case M_SET_CURRENT_CLIP:
		/* 1068:0566: the token only with exactly one argument (1190:4e80: 0 if not a
		 * token), otherwise clip 1200 */
		sprite_set_clip(a, nargs == 1 ? (uint16_t)val_token_of(&args[0]) : CLIP_FORWARD);
		return val_empty();
	case M_INCREMENT_FRAME: {
		/* 1068:0b7c: bool only with exactly one argument (1190:4e06) */
		int loop = nargs == 1 ? val_bool_of(&args[0]) == 1 : 0;
		if (!sprite_next(a) && loop)
			sprite_set_initial(a);
		return val_empty();
	}
	case M_DECREMENT_FRAME: {
		int to_final = nargs == 1 ? val_bool_of(&args[0]) == 1 : 0;
		if (!sprite_prev(a) && to_final)
			sprite_set_final(a);
		return val_empty();
	}
	case M_GET_CURRENT_CLIP_ID:
		return val_token(s->active.id);
	case M_IS_PLAYING: /* not in LIONKING.EXE (later engines) */
		if (script_extensions)
			return val_bool(s->playing);
		break;
	}
	*handled = 0;
	return val_empty();
}

static void sprite_event(MsActor *a, int ev, const MsValue *arg) {
	if (ev == EV_SPRITE_MOVIE_END)
		actor_run_response(a, ev, arg);
	else
		ms_warn("sprite %u: no handler for event 0x%x", a->id, ev);
}

static void sprite_alarm(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (sprite_next(a)) {
		if (s->current == s->active.last) {
			s->playing = 0;
			a->start_time = 0;
			MsValue v = val_token(s->active.id);
			rt_queue_actor_event(a->id, EV_SPRITE_MOVIE_END, &v);
		}
		sprite_schedule(a);
	} else {
		sprite_stop(a);
	}
}

static SpriteFrame *sprite_frame(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (!s->asset || s->asset->nframes == 0)
		return NULL;
	if (s->current < 0 || s->current >= s->asset->nframes)
		s->current = s->asset->nframes - 1;
	return &s->asset->frames[s->current];
}

MsRect sprite_bbox(MsActor *a) {
	SpriteFrame *f = sprite_frame(a);
	if (!f)
		return a->box;
	MsRect r = {(int16_t)(a->box.x + f->origin.x), (int16_t)(a->box.y + f->origin.y),
		(int16_t)f->bmp->w, (int16_t)f->bmp->h};
	return r;
}

static void sprite_draw(MsActor *a) {
	SpriteFrame *f = sprite_frame(a);
	if (f && a->visible)
		rt_draw_bitmap(f->bmp, a->box.x + f->origin.x, a->box.y + f->origin.y, a->dissolve);
}

static void sprite_destroy(MsActor *a) {
	Sprite *s = X(Sprite, a);
	if (s->asset && --s->asset->refs == 0) {
		for (int i = 0; i < s->asset->nframes; i++)
			bitmap_free(s->asset->frames[i].bmp);
		free(s->asset->frames);
		free(s->asset);
	}
	s->asset = NULL;
}

const MsActorType ms_type_sprite = {"Sprite", sizeof(Sprite), 1, sprite_init, sprite_param,
	sprite_chunk, NULL, sprite_method, sprite_event, sprite_alarm, sprite_draw,
	sprite_destroy};

/* ====================================================================== movie */

enum { BLIT_UNCOMPRESSED = 1, BLIT_UNCOMPRESSED_DELTA = 2, BLIT_COMPRESSED_DELTA = 3 };

typedef struct {
	uint32_t index;
	uint32_t keyframe_end;
	MsBitmap *bmp;
	uint8_t *decoded; /* decompressed copy when used as a keyframe */
} MovieImage;

typedef struct {
	uint32_t layer;
	int blit;
	uint32_t start, end;
	MsPoint left_top, key_diff;
	int z;
	uint32_t index, key_index;
	int keep_after_end;
	int shown; /* in the on-screen list */
	MovieImage *image, *key;
} MovieFrame;

typedef struct {
	MsAudioSeq seq;
	MovieImage **images;
	int nimages;
	MovieFrame **frames;
	int nframes;
	MovieFrame **pending; /* frames not yet shown, in start order */
	int npending;
	MovieFrame **onscreen;
	int nonscreen;
	uint32_t frames_channel, sound_channel;
	uint32_t full_time;
	uint16_t chunk_count;
	double frame_rate;
	int stream_read;
	int playing;
	int paused;              /* states 7/8 of the movie state machine (paused while playing) */
	uint32_t paused_elapsed; /* movie time when paused */
	int has_still;
	int cache;
} Movie;

static void movie_stop(MsActor *a, int event);

static void movie_sound_cb(MsAudioSeq *s, int state, int reason, void *user) {
	(void)s;
	MsActor *a = user;
	/* a sound failure stops the movie with reason 1, MovieFailure (1190:f1a4 -> 1190:f2a6) */
	if (state == AUDIO_STOPPED && reason == STOP_FAILURE)
		movie_stop(a, EV_MOVIE_FAILURE);
}

static void movie_init(MsActor *a) {
	Movie *m = X(Movie, a);
	m->seq.voice = -1;
	m->seq.state = AUDIO_STOPPED;
	m->seq.on_state = movie_sound_cb;
	m->seq.user = a;
}

static int movie_param(MsActor *a, MsChunk *c, uint16_t p) {
	Movie *m = X(Movie, a);
	switch (p) {
	case AH_ACTOR_ID: {
		uint16_t dup = ms_t_u16(c);
		if (dup != a->id)
			ms_warn("movie %u: duplicate actor id %u", a->id, dup);
		return 1;
	}
	case AH_CHANNEL_IDENT:
		register_main_channel(a, c);
		return 1;
	case AH_STARTUP:
		a->visible = ms_t_u8(c) != 0;
		return 1;
	case AH_DISCARD_AFTER_USE:
	case AH_INSTALL_TYPE:
		ms_t_u8(c);
		return 1;
	case AH_CACHING_ENABLED:
		m->cache = ms_t_u8(c) != 0;
		return 1;
	case AH_MOVIE_AUDIO_CHANNEL:
		m->sound_channel = ms_t_channel(c);
		god_register_channel(m->sound_channel, a, CH_MOVIE_SOUND);
		return 1;
	case AH_MOVIE_ANIMATION_CHANNEL:
		m->frames_channel = ms_t_channel(c);
		god_register_channel(m->frames_channel, a, CH_MOVIE_FRAMES);
		return 1;
	case AH_SOUND_INFO:
		audio_seq_read_params(&m->seq, c);
		return 1;
	case AH_STREAM_MOVIE_PROXY_INFO:
		ms_fatal("movie %u: stream movie proxies (later engines) are not supported", a->id);
	}
	return 0;
}

static void movie_read_frames_chunk(MsActor *a, MsChunk *c) {
	Movie *m = X(Movie, a);
	uint16_t section = ms_t_u16(c);
	if (section == 0x06a9) {
		/* image data */
		MsImageInfo info = bitmap_read_info(c);
		MovieImage *im = calloc(1, sizeof(MovieImage));
		im->index = ms_t_u32(c);
		im->keyframe_end = ms_t_u32(c);
		im->bmp = bitmap_read(c, &info, 0);
		m->images = realloc(m->images, sizeof(MovieImage *) * (size_t)(m->nimages + 1));
		m->images[m->nimages++] = im;
	} else if (section == 0x06aa) {
		/* frame data */
		uint16_t n = ms_t_u16(c);
		int first_gen = ms_god.ver_major == 0;
		for (int i = 0; i < n; i++) {
			MovieFrame *f = calloc(1, sizeof(MovieFrame));
			if (!first_gen)
				f->layer = ms_t_u32(c);
			f->blit = ms_t_u16(c);
			f->start = ms_t_u32(c);
			f->end = ms_t_u32(c);
			f->left_top.x = (int16_t)ms_t_u16(c);
			f->left_top.y = (int16_t)ms_t_u16(c);
			if (!first_gen) {
				f->z = ms_t_s16(c);
				f->key_diff.x = ms_t_s16(c);
				f->key_diff.y = ms_t_s16(c);
			}
			f->index = ms_t_u32(c);
			f->key_index = ms_t_u32(c);
			f->keep_after_end = ms_t_u8(c);
			f->layer = 0; /* no proxies in first-generation titles */
			for (int k = 0; k < m->nimages; k++)
				if (m->images[k]->index == f->index) {
					f->image = m->images[k];
					break;
				}
			if (f->key_index)
				for (int k = 0; k < m->nimages; k++)
					if (m->images[k]->index == f->key_index) {
						f->key = m->images[k];
						break;
					}
			m->frames = realloc(m->frames, sizeof(MovieFrame *) * (size_t)(m->nframes + 1));
			m->frames[m->nframes++] = f;
		}
	} else {
		ms_fatal("movie %u: unknown frames section 0x%x", a->id, section);
	}
	for (int i = 0; i < m->nframes; i++) {
		if (m->frames[i]->end > m->full_time)
			m->full_time = m->frames[i]->end;
		if (m->frames[i]->keep_after_end)
			m->has_still = 1;
	}
	if (m->has_still) {
		m->pending = realloc(m->pending, sizeof(MovieFrame *) * (size_t)(m->nframes + 1));
		memcpy(m->pending, m->frames, sizeof(MovieFrame *) * (size_t)m->nframes);
		m->npending = m->nframes;
	}
}

static void movie_chunk(MsActor *a, int sub, MsChunk *c) {
	Movie *m = X(Movie, a);
	if (sub == CH_MOVIE_FRAMES) {
		movie_read_frames_chunk(a, c);
	} else if (sub == CH_MOVIE_SOUND) {
		audio_seq_add_chunk(&m->seq, c);
	} else {
		uint16_t section = ms_t_u16(c);
		if (section == 0x06a8) {
			m->chunk_count = ms_t_u16(c);
			m->frame_rate = ms_t_double(c);
			for (int i = 0; i < m->chunk_count; i++)
				ms_t_u32(c);
		} else if (section == 0x06ab) {
			/* chunk marker: streaming schedule of the original, nothing to do */
			c->pos = c->end;
		} else {
			ms_fatal("movie %u: unknown section 0x%x", a->id, section);
		}
	}
}

static MsRect movie_frame_box(MsActor *a, MovieFrame *f) {
	MsRect r = {(int16_t)(a->box.x + f->left_top.x), (int16_t)(a->box.y + f->left_top.y), 0, 0};
	if (f->image && f->image->bmp) {
		r.w = (int16_t)f->image->bmp->w;
		r.h = (int16_t)f->image->bmp->h;
	}
	return r;
}

static void movie_stop(MsActor *a, int event);

/* Movie time: frozen while paused (LIONKING.EXE 1190:f886 keeps the elapsed time in +0xb2,
 * 1190:f90c restarts the clock in +0xba). */
static uint32_t movie_time(MsActor *a) {
	Movie *m = X(Movie, a);
	if (!m->playing)
		return 0;
	if (m->paused)
		return m->paused_elapsed;
	/* while its sound plays, the movie follows the sound's position (1190:ef04: with +0xdc
	 * set the time comes from the audio object, 1198:a902), otherwise its own clock */
	if (m->seq.state == AUDIO_PLAYING && m->seq.voice >= 0)
		return audio_seq_position_ms(&m->seq);
	return rt_now() - a->start_time;
}

static void movie_clear_onscreen(Movie *m) {
	for (int i = 0; i < m->nonscreen; i++)
		m->onscreen[i]->shown = 0;
	m->nonscreen = 0;
}

/* Returns 1 if the frames on screen changed. The original shows the frames of time
 * _ftol(t * 1000) - 1 ms (1190:fbc4 -> 1198:b732). */
static int movie_update_frames(MsActor *a) {
	Movie *m = X(Movie, a);
	uint32_t t = movie_time(a);
	int changed = 0;
	if (t > 0)
		t--;
	/* new frames (pending frames are in start order) */
	while (m->npending > 0 && t >= m->pending[0]->start) {
		MovieFrame *f = m->pending[0];
		memmove(m->pending, m->pending + 1, sizeof(MovieFrame *) * (size_t)(m->npending - 1));
		m->npending--;
		/* on-screen frames are kept sorted by z (higher z first, stable) */
		m->onscreen = realloc(m->onscreen, sizeof(MovieFrame *) * (size_t)(m->nonscreen + 1));
		int pos = m->nonscreen;
		while (pos > 0 && m->onscreen[pos - 1]->z < f->z)
			pos--;
		memmove(m->onscreen + pos + 1, m->onscreen + pos,
			sizeof(MovieFrame *) * (size_t)(m->nonscreen - pos));
		m->onscreen[pos] = f;
		m->nonscreen++;
		f->shown = 1;
		changed = 1;
		actor_invalidate(a);
	}
	/* frames that ended */
	for (int i = 0; i < m->nonscreen;) {
		MovieFrame *f = m->onscreen[i];
		if (t >= f->end) {
			rt_invalidate(movie_frame_box(a, f));
			f->shown = 0;
			changed = 1;
			memmove(m->onscreen + i, m->onscreen + i + 1,
				sizeof(MovieFrame *) * (size_t)(m->nonscreen - i - 1));
			m->nonscreen--;
			if (m->nonscreen == 0 && t >= m->full_time) {
				movie_stop(a, EV_MOVIE_END);
				break;
			}
		} else {
			i++;
		}
	}
	return changed;
}

/* timePlay (LIONKING.EXE 1190:f998 case 0xce) sends the state machine "stop, reason 2" and then
 * "start": a movie that is already playing (or paused) is aborted (MovieAbort 0x15, entry
 * action of the stopped state 1190:f2e8) and starts again; a stopped movie ignores the stop
 * (transition table built by 1190:e3a0, see docs\ORIGINAL_GRAPHICS.md). */
static void movie_play(MsActor *a) {
	Movie *m = X(Movie, a);
	if (m->playing)
		movie_stop(a, EV_MOVIE_ABORT);
	if (!rt_grant_sound_device(a)) {
		ms_warn("movie %u: device grant failed", a->id);
		return;
	}
	if (!m->stream_read) {
		m->stream_read = 1;
		god_read_stream(a->id);
	}
	actor_setup_next_time_timer(a);
	rt_register_sync(a);
	audio_seq_start(&m->seq);
	m->pending = realloc(m->pending, sizeof(MovieFrame *) * (size_t)(m->nframes + 1));
	memcpy(m->pending, m->frames, sizeof(MovieFrame *) * (size_t)m->nframes);
	m->npending = m->nframes;
	movie_clear_onscreen(m);
	m->playing = 1;
	m->paused = 0;
	a->start_time = rt_now();
	a->last_processed = 0;
	rt_queue_actor_event(a->id, EV_MOVIE_BEGIN, NULL);
	movie_update_frames(a);
}

/* Stop with the event that the stopped state posts (LIONKING.EXE 1190:f2e8: reason 1
 * MovieFailure 0x16, 2 MovieAbort 0x15, 3 MovieStopped 0x1f, 4 MovieEnd 0x0f). A stopped movie
 * ignores it (no event). */
static void movie_stop(MsActor *a, int event) {
	Movie *m = X(Movie, a);
	if (!m->playing)
		return;
	m->playing = 0;
	m->paused = 0;
	rt_release_sound_device(a);
	MsAudioSeq *seq = &m->seq;
	void (*cb)(MsAudioSeq *, int, int, void *) = seq->on_state;
	seq->on_state = NULL; /* the movie reports its own end */
	audio_seq_stop(seq);
	seq->on_state = cb;
	actor_stop_alarm(a);
	rt_unregister_sync(a);
	m->npending = 0;
	if (m->has_still) {
		memcpy(m->pending, m->frames, sizeof(MovieFrame *) * (size_t)m->nframes);
		m->npending = m->nframes;
	}
	movie_clear_onscreen(m);
	rt_queue_actor_event(a->id, event, NULL);
	actor_invalidate(a);
	movie_update_frames(a);
}

/* called by the runtime before each redraw while the movie is registered for sync */
/* Pre-display service of a playing movie (LIONKING.EXE 1190:fbc4), also when hidden. The
 * original answers 2 (skip this screen update) when the stream chunk needed now has not been
 * read yet; the port reads the whole stream at play, so that never happens here. */
int movie_pre_display_sync(MsActor *a) {
	Movie *m = X(Movie, a);
	if (!m->playing)
		return RT_SYNC_IDLE;
	return movie_update_frames(a) ? RT_SYNC_CHANGED : RT_SYNC_IDLE;
}

int movie_device_give_up(MsActor *a) {
	movie_stop(a, EV_MOVIE_STOPPED);
	return 0;
}

/* timePause / timeResume (LIONKING.EXE 1190:f998 cases 0xd0, 0xd1): events 3 and 4 of the
 * state machine. Only a playing movie pauses (state 2 -> 7, 1190:f886: the clock and the
 * sound pause) and only a paused one resumes (7 -> 8, 1190:f90c); neither posts an event. */
static void movie_pause(MsActor *a, int pause) {
	Movie *m = X(Movie, a);
	if (!m->playing || m->paused == pause)
		return;
	if (pause) {
		m->paused_elapsed = movie_time(a);
		m->paused = 1;
		actor_stop_alarm(a);
		audio_seq_pause(&m->seq);
	} else {
		a->start_time = rt_now() - m->paused_elapsed;
		m->paused = 0;
		audio_seq_resume(&m->seq);
		actor_setup_next_time_timer(a);
	}
}

static MsValue movie_method(MsActor *a, uint32_t mth, MsValue *args, int nargs, int *handled) {
	Movie *m = X(Movie, a);
	(void)args;
	switch (mth) {
	case M_SPATIAL_SHOW:
	case M_SPATIAL_HIDE: {
		if (nargs)
			ms_warn("movie %u: show/hide of a proxy (later engines)", a->id);
		/* 1190:f998: nothing if the flag is already set that way; otherwise the area is
		 * invalidated only if the visibility test (1190:fb46: flag, dissolve not 0, a frame
		 * on screen) changes */
		int v = mth == M_SPATIAL_SHOW;
		if (v != a->visible) {
			int before = a->visible && a->dissolve != 0.0 && m->nonscreen > 0;
			a->visible = v;
			if ((a->visible && a->dissolve != 0.0 && m->nonscreen > 0) != before)
				actor_invalidate(a);
		}
		return val_empty();
	}
	case M_TIME_PLAY:
		movie_play(a);
		return val_empty();
	case M_TIME_STOP:
		movie_stop(a, EV_MOVIE_STOPPED);
		return val_empty();
	case M_TIME_PAUSE:
		movie_pause(a, 1);
		return val_empty();
	case M_TIME_RESUME:
		movie_pause(a, 0);
		return val_empty();
	case 0x140: /* ignored by the movie class (1190:f998) */
		return val_empty();
	case M_IS_PLAYING: /* not in LIONKING.EXE (later engines) */
		if (!script_extensions)
			break;
		return val_bool(m->playing);
	case M_GET_LEFT_X:
		return val_float(a->box.x);
	case M_GET_TOP_Y:
		return val_float(a->box.y);
	}
	*handled = 0;
	return val_empty();
}

static void movie_event(MsActor *a, int ev, const MsValue *arg) {
	(void)arg;
	uint16_t id = a->id;
	/* the state was already changed when the event was posted (movie_stop): a movie aborted
	 * and started again by timePlay must stay playing when its MovieAbort arrives */
	if (ev == EV_MOVIE_END) {
		actor_trigger_remaining_time_responses(a);
		if (god_actor(id) != a)
			return;
	}
	actor_run_response(a, ev, NULL);
}

static void movie_alarm(MsActor *a) {
	uint16_t id = a->id;
	actor_process_time_responses(a);
	if (god_actor(id) != a)
		return;
	actor_stop_alarm(a);
	actor_setup_next_time_timer(a);
}

static void movie_load_complete(MsActor *a) {
	movie_update_frames(a);
}

/* LIONKING.EXE 1190:fb80 -> 1198:bb28: the frames on screen are drawn in the order of the
 * draw list, which 1198:b732 fills walking the frames from the last read to the first (so a
 * frame read earlier is drawn later, on top); only frames of type 1 are drawn, with the
 * generic blit and the movie's dissolve factor (10a0:0000). Frames of any other type (the
 * "delta" types 2 and 3 of later engines) and the second image of a frame (+0x12) are only
 * invalidated and loaded, never drawn. Every Lion King movie frame is type 1 without a
 * second image. */
static void movie_draw(MsActor *a) {
	Movie *m = X(Movie, a);
	for (int i = m->nframes - 1; i >= 0; i--) {
		MovieFrame *f = m->frames[i];
		if (!f->shown || f->blit != BLIT_UNCOMPRESSED || !f->image || !f->image->bmp)
			continue;
		MsRect b = movie_frame_box(a, f);
		rt_draw_bitmap(f->image->bmp, b.x, b.y, a->dissolve);
	}
}

MsRect movie_bbox(MsActor *a) {
	return a->box;
}

static void movie_destroy(MsActor *a) {
	Movie *m = X(Movie, a);
	if (m->playing) {
		m->seq.on_state = NULL;
		audio_seq_stop(&m->seq);
	}
	rt_release_sound_device(a);
	if (m->frames_channel)
		god_unregister_channel(m->frames_channel);
	if (m->sound_channel)
		god_unregister_channel(m->sound_channel);
	for (int i = 0; i < m->nimages; i++) {
		bitmap_free(m->images[i]->bmp);
		free(m->images[i]->decoded);
		free(m->images[i]);
	}
	for (int i = 0; i < m->nframes; i++)
		free(m->frames[i]);
	free(m->images);
	free(m->frames);
	free(m->pending);
	free(m->onscreen);
	audio_seq_free(&m->seq);
}

const MsActorType ms_type_movie = {"Movie", sizeof(Movie), 1, movie_init, movie_param,
	movie_chunk, movie_load_complete, movie_method, movie_event, movie_alarm, movie_draw,
	movie_destroy};
