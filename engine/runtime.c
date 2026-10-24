/* The player: event loop, timer service, document (screen branches and context loading),
 * root stage (drawing order, dirty region, mouse and keyboard dispatch), display updates and
 * sound device ownership. Follows the original player as reconstructed by ScummVM
 * (events.cpp, clients.cpp, actors/stage.cpp, graphics.cpp). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script.h"
#include "video.h"

/* actors.c / audio.c / cursors.c */
extern int hotspot_in_bounds(MsActor *a, int x, int y);
extern int hotspot_is_active(MsActor *a);
extern uint16_t hotspot_cursor(MsActor *a);
extern int movie_pre_display_sync(MsActor *a);
extern int zazu_is_active(MsActor *a);
extern int zazu_mouse_interesting(MsActor *a, int x, int y);
extern int zazu_mouse_inside(MsActor *a);
extern int zazu_mouse_is_down(MsActor *a);
extern void zazu_mouse_event(MsActor *a, int ev);
extern int sound_device_give_up(MsActor *a);
extern int movie_device_give_up(MsActor *a);
extern void audio_service(void);
extern void cursors_new(uint16_t id, int type, uint16_t platform_id, const char *resource);
extern void cursors_dispose(uint16_t id);
extern int cursors_about_box(char *title, size_t title_cap, char *text, size_t cap, char *button,
                             size_t button_cap);

#define ROOT_STAGE_ID 2
#define DOCUMENT_ID 1

static uint32_t clock_base, clock_paused_at;
static int clock_paused;
static int running = 1;

/* The engine clock (LIONKING.EXE 1190:05a4, object DS:025A): seconds of timeGetTime()
 * (1188:1332, 1 ms granularity) minus a base; while paused it stays at the pause time. */
uint32_t rt_now(void) {
	return (clock_paused ? clock_paused_at : SDL_GetTicks()) - clock_base;
}

/* pause 1190:04a2 (only if not paused), resume 1190:04fa: the base moves on by the time
 * spent paused, so timers keep their remaining time */
static void clock_pause(int pause) {
	if (pause && !clock_paused) {
		clock_paused_at = SDL_GetTicks();
		clock_paused = 1;
	} else if (!pause && clock_paused) {
		clock_base += SDL_GetTicks() - clock_paused_at;
		clock_paused = 0;
	}
}

static uint32_t rng_state = 0x2545f491;

uint32_t rt_random(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* --seed: fixed random sequence per test case (xorshift needs a non-zero state) */
void rt_set_seed(uint32_t seed) {
	rng_state = seed ? seed : 0x2545f491;
}

void rt_quit(void) {
	if (script_trace)
		fprintf(stderr, "[%6u] quit\n", rt_now());
	running = 0;
}

/* ===================================================================== events */

enum { Q_ACTOR, Q_BRANCH, Q_ENABLE_AUTO_UPDATE, Q_ALARM, Q_MOUSE, Q_KEY };
enum { MOUSE_DOWN, MOUSE_UP, MOUSE_MOVED, MOUSE_ENTER_EXIT, MOUSE_OUT_OF_FOCUS };

typedef struct {
	int kind;
	uint16_t actor;
	int event;
	MsValue arg;
	uint16_t screen;
	int flag;
	uint32_t token;
	int x, y;
} QEvent;

static QEvent *incoming;
static int nincoming, capincoming;

static void queue(QEvent e) {
	if (nincoming == capincoming) {
		capincoming = capincoming ? capincoming * 2 : 64;
		incoming = realloc(incoming, sizeof(QEvent) * (size_t)capincoming);
	}
	incoming[nincoming++] = e;
}

void rt_queue_actor_event(uint16_t actor_id, int event, const MsValue *arg) {
	QEvent e;
	memset(&e, 0, sizeof(e));
	e.kind = Q_ACTOR;
	e.actor = actor_id;
	e.event = event;
	e.arg = arg ? val_copy(arg) : val_empty();
	queue(e);
}

void rt_queue_branch(uint16_t screen_id, int disable_auto_update) {
	QEvent e;
	memset(&e, 0, sizeof(e));
	e.kind = Q_BRANCH;
	e.screen = screen_id;
	e.flag = disable_auto_update;
	queue(e);
}

void rt_queue_enable_auto_update(uint32_t token) {
	QEvent e;
	memset(&e, 0, sizeof(e));
	e.kind = Q_ENABLE_AUTO_UPDATE;
	e.token = token;
	queue(e);
}

static void queue_mouse(int type, int x, int y) {
	QEvent e;
	memset(&e, 0, sizeof(e));
	e.kind = Q_MOUSE;
	e.event = type;
	e.x = x;
	e.y = y;
	queue(e);
}

/* A key is an engine event like the mouse (class 4, subtype 0xd, LIONKING.EXE 1188:03d4 and
 * 1188:0474 post it with 1188:fb48): it is dispatched in the next pass of the loop, not at
 * once. */
static void queue_key(int code) {
	QEvent e;
	memset(&e, 0, sizeof(e));
	e.kind = Q_KEY;
	e.event = code;
	queue(e);
}

/* ======================================================= scripts and tracing */

MsValue rt_run_code(uint16_t actor_id, const uint8_t *code, uint32_t size, MsValue *args, int nargs) {
	return script_run(actor_id, code, size, args, nargs);
}

MsValue rt_call_function(uint32_t function_id, MsValue *args, int nargs) {
	return script_call_function(function_id, args, nargs);
}

void rt_trace_event(MsActor *a, int ev, const MsValue *arg) {
	if (!script_trace || !a)
		return;
	fprintf(stderr, "[%6u] actor %u (%s%s%s) event 0x%x%s%s\n", rt_now(), a->id, a->vt->name,
		a->name ? " " : "", a->name ? a->name : "", ev, arg && arg->type ? " " : "",
		arg && arg->type ? val_debug(arg) : "");
}

/* ================================================================ root stage */

typedef struct {
	MsActor *a;
	uint32_t serial;
} StageEntry;

static StageEntry *stage;
static int nstage, capstage;
static uint32_t stage_serial;

/* Dirty rectangles of the root stage (list +0x36 of the stage object, emptied after each
 * update by 1190:a12c), in the order they were added, not clipped. */
static MsRect *dirty;
static int ndirty, capdirty;

static int rect_empty(MsRect r) {
	return r.w <= 0 || r.h <= 0;
}

static MsRect rect_intersect(MsRect a, MsRect b) {
	int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
	int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
	int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
	MsRect r = {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
	if (r.w < 0)
		r.w = 0;
	if (r.h < 0)
		r.h = 0;
	return r;
}

static int rect_contains(MsRect a, MsRect b) {
	return a.x <= b.x && a.y <= b.y && a.x + a.w >= b.x + b.w && a.y + a.h >= b.y + b.h;
}

/* Adds a rectangle to a rectangle list as LIONKING.EXE 1190:7c76: an empty rectangle is
 * ignored; the list is scanned from the first entry: if an entry contains the new rectangle
 * nothing is added; if the new rectangle contains an entry, that entry is replaced and the
 * scan stops (other entries it contains stay); otherwise it is appended. No merging and no
 * limit on the number of rectangles. */
static void rect_list_add(MsRect **list, int *n, int *cap, MsRect r) {
	if (r.w == 0 || r.h == 0)
		return;
	for (int i = 0; i < *n; i++) {
		if (rect_contains((*list)[i], r))
			return;
		if (rect_contains(r, (*list)[i])) {
			(*list)[i] = r;
			return;
		}
	}
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*list = realloc(*list, sizeof(MsRect) * (size_t)*cap);
	}
	(*list)[(*n)++] = r;
}

/* The root stage collects invalidated rectangles unclipped (1198:961a -> 1190:7c76). */
void rt_invalidate(MsRect r) {
	rect_list_add(&dirty, &ndirty, &capdirty, r);
}

/* Children are kept by z, higher z first (drawn first, i.e. behind). The original keeps the
 * list in ascending order of (z, offset of the actor object) (comparator 1198:9784, insertion
 * 1198:fac0, re-sort with qsort 1198:fa9c) and draws it from the last entry to the first
 * (1198:9d0e): the same order as here, except that actors with the same z are ordered by
 * their heap address (lower address in front), which cannot be reproduced; here the actor
 * added later is in front. */
static int stage_before(const StageEntry *x, const StageEntry *y) {
	if (x->a->z != y->a->z)
		return x->a->z > y->a->z;
	return x->serial < y->serial;
}

static void stage_sort(void) {
	for (int i = 1; i < nstage; i++) {
		StageEntry e = stage[i];
		int j = i;
		while (j > 0 && stage_before(&e, &stage[j - 1])) {
			stage[j] = stage[j - 1];
			j--;
		}
		stage[j] = e;
	}
}

static int stage_visible(MsActor *a);

static void stage_add(MsActor *a) {
	for (int i = 0; i < nstage; i++)
		if (stage[i].a == a)
			return;
	if (nstage == capstage) {
		capstage = capstage ? capstage * 2 : 128;
		stage = realloc(stage, sizeof(StageEntry) * (size_t)capstage);
	}
	stage[nstage].a = a;
	stage[nstage].serial = stage_serial++;
	nstage++;
	stage_sort();
	/* adding a child invalidates only that child, if it is visible (LIONKING.EXE 1198:9c36:
	 * vtable +0x20, then +0x54) */
	if (stage_visible(a))
		actor_invalidate(a);
}

/* removing a visible child invalidates its area first (LIONKING.EXE 1198:9c8e) */
void rt_stage_remove(MsActor *a) {
	for (int i = 0; i < nstage; i++)
		if (stage[i].a == a) {
			if (stage_visible(a))
				actor_invalidate(a);
			memmove(stage + i, stage + i + 1, sizeof(StageEntry) * (size_t)(nstage - i - 1));
			nstage--;
			return;
		}
}

void rt_stage_resort(void) {
	stage_sort();
}

static int stage_visible(MsActor *a) {
	if (a->type == ACTOR_HOTSPOT)
		return 0;
	return a->visible;
}

/* Clip region of a redraw: the display context's region (10c0) gets the dirty list one
 * rectangle at a time (1198:9676 -> 10a8:00ea: clear 10c0:0438, add each 10c0:0756 ->
 * 10c0:07fc), each clipped to the screen and added with the same rule (1190:7c76), then
 * intersected with the stage box (10c0:0b6a). Every blit walks this list from the first
 * rectangle. */
static MsRect *region;
static int nregion, capregion;

static int region_touches(MsRect r) {
	for (int i = 0; i < nregion; i++)
		if (!rect_empty(rect_intersect(region[i], r)))
			return 1;
	return 0;
}

/* Stage drawing (1198:9d0e): children from back to front, each drawn once if it is visible
 * and its box meets the region (10a8:0090 -> 10c0:07c4); the actor's blits then go through
 * the rectangles of the region themselves (video.c). */
static void draw_region(void) {
	video_set_clip_list(region, nregion);
	for (int i = 0; i < nstage; i++) {
		MsActor *a = stage[i].a;
		if (!stage_visible(a) || !a->vt->draw)
			continue;
		if (!region_touches(actor_bbox(a)))
			continue;
		a->vt->draw(a);
	}
	video_set_clip_list(NULL, 0);
}

/* ========================================================= display updates */

static int auto_update = 1, force_flush;
static uint32_t disable_token;

/* Copy of the frame buffer to the window. LIONKING.EXE copies at every screen event
 * (1190:8e2e) and never waits for the monitor (1188:fb70 never sleeps), so a state that only
 * lasts until the next pass of the engine loop is on screen for microseconds: e.g. a movie
 * that ends in the pre-display service (1190:fbc4 -> fda4 -> f2a6 stop, reason 4) is erased
 * at once, while the MovieEnd that plays it again (menu_timon_idle in the menu) waits for the
 * engine queue of the next pass (1188:fd14). Here every present lasts a whole vsync frame,
 * so such a state flickered: the copy waits until the engine queue is empty (drawing is
 * unchanged), at most PRESENT_MAX_DEFER ms after the last one. */
#define PRESENT_MAX_DEFER 50
static uint32_t last_present;

static void present_screen(void) {
	uint32_t t = SDL_GetTicks();
	if (nincoming > 0 && t - last_present < PRESENT_MAX_DEFER)
		return;
	last_present = t;
	video_present();
}

static void flush_to_display(void) {
	present_screen();
	video_do_transition_on_sync();
}

static void update_dirty(void) {
	MsRect screen = {0, 0, SCREEN_W, SCREEN_H};
	nregion = 0;
	for (int i = 0; i < ndirty; i++)
		rect_list_add(&region, &nregion, &capregion, rect_intersect(dirty[i], screen));
	if (nregion)
		draw_region();
	ndirty = 0;
	flush_to_display();
}

/* Full redraw (1190:a10a -> 1198:965e): no dirty list, the region is the whole stage box
 * (1198:9d0e adds the region bounds when there is no enclosing clip, 10c0:07fc). */
static void update_all(void) {
	MsRect screen = {0, 0, SCREEN_W, SCREEN_H};
	nregion = 0;
	rect_list_add(&region, &nregion, &capregion, screen);
	draw_region();
	ndirty = 0;
	flush_to_display();
}

void rt_perform_update_dirty(void) {
	update_dirty();
}

uint32_t rt_disable_auto_update(void) {
	auto_update = 0;
	force_flush = 0;
	if (++disable_token == 0)
		disable_token = 1;
	return disable_token;
}

void rt_enable_auto_update(uint32_t token) {
	if (token == disable_token) {
		auto_update = 1;
		force_flush = 1;
	}
}

/* The screen event (LIONKING.EXE 1190:8e2e): after auto update has been enabled again the
 * whole stage is redrawn (1190:8ec8 -> 1190:a10a), otherwise only the dirty region
 * (1190:8e76 -> 1190:a0e8); both then copy to the window and run the "on sync" transition. */
static void auto_update_and_flush(void) {
	if (auto_update && force_flush) {
		update_all();
		force_flush = 0;
	} else if (auto_update && ndirty > 0) {
		update_dirty();
	} else {
		present_screen(); /* mouse cursor and the like */
	}
}

/* ============================================================== pre-display sync */

static MsActor *sync_clients[64];
static int nsync;

void rt_register_sync(MsActor *a) {
	for (int i = 0; i < nsync; i++)
		if (sync_clients[i] == a)
			return;
	if (nsync < 64)
		sync_clients[nsync++] = a;
}

void rt_unregister_sync(MsActor *a) {
	for (int i = 0; i < nsync; i++)
		if (sync_clients[i] == a) {
			sync_clients[i] = sync_clients[--nsync];
			return;
		}
}

/* The services of the engine loop (LIONKING.EXE 1188:f6b2, platform vtable +0x18): every
 * registered client (only playing movies, 1198:1f36 / 1198:1f62; service 1190:fbc4) answers
 * RT_SYNC_CHANGED (0 in the exe: something changed), RT_SYNC_IDLE (1) or RT_SYNC_SKIP (2: a
 * frame is already late, do not update the screen in this pass). The result starts as idle;
 * a changed answer makes it "changed" unless it is already "skip"; any skip makes it skip. */
static int pre_display_sync(void) {
	int result = RT_SYNC_IDLE;
	MsActor *copy[64];
	int n = nsync;
	memcpy(copy, sync_clients, sizeof(MsActor *) * (size_t)n);
	for (int i = 0; i < n; i++) {
		int still = 0;
		for (int k = 0; k < nsync; k++)
			if (sync_clients[k] == copy[i])
				still = 1;
		if (!still || copy[i]->type != ACTOR_MOVIE)
			continue;
		int r = movie_pre_display_sync(copy[i]);
		if (r == RT_SYNC_SKIP)
			result = RT_SYNC_SKIP;
		else if (r == RT_SYNC_CHANGED && result != RT_SYNC_SKIP)
			result = RT_SYNC_CHANGED;
	}
	return result;
}

/* =========================================================== device owner */

static MsActor *sound_users[64];
static int nsound_users;

static void give_up(MsActor *a) {
	if (a->type == ACTOR_SOUND)
		sound_device_give_up(a);
	else if (a->type == ACTOR_MOVIE)
		movie_device_give_up(a);
}

int rt_grant_sound_device(MsActor *a) {
	if (!ms_god.allow_multiple_sounds) {
		MsActor *copy[64];
		int n = nsound_users;
		memcpy(copy, sound_users, sizeof(MsActor *) * (size_t)n);
		for (int i = 0; i < n; i++) {
			if (copy[i] == a)
				return 1;
			give_up(copy[i]);
		}
		nsound_users = 0;
	}
	if (nsound_users < 64)
		sound_users[nsound_users++] = a;
	return 1;
}

void rt_release_sound_device(MsActor *a) {
	for (int i = 0; i < nsound_users; i++)
		if (sound_users[i] == a) {
			memmove(sound_users + i, sound_users + i + 1, sizeof(MsActor *) * (size_t)(nsound_users - i - 1));
			nsound_users--;
			return;
		}
}

void rt_set_multiple_sounds(int allow) {
	if (allow) {
		ms_god.allow_multiple_sounds = 1;
		nsound_users = 0;
	} else {
		if (!ms_god.allow_multiple_sounds) {
			MsActor *copy[64];
			int n = nsound_users;
			memcpy(copy, sound_users, sizeof(MsActor *) * (size_t)n);
			for (int i = 0; i < n; i++)
				give_up(copy[i]);
		}
		nsound_users = 0;
		ms_god.allow_multiple_sounds = 0;
	}
}

void rt_set_multiple_streams(int allow) {
	ms_god.allow_multiple_streams = allow;
}

/* ================================================================== document */

/* The context loader of the original (LIONKING.EXE, object DS:031A, constructor 1190:2d40):
 * +0x10 screen being branched to, +0x12 current screen, +0x14 context being read, +0x16
 * entry screen, +0x18 boot stream, +0x1a stream being read. It reads only the context of the
 * screen: no parent contexts, no queue, no locks (the Lion King's screen contexts have only
 * the boot context 1100 as parent, always loaded). The display observer (DS:031E, 1190:a1ae)
 * disables the auto update at the branch (1190:a1f8) and enables it again at the entry
 * (1190:a24a). */
static uint16_t current_screen, loading_screen, loading_context;
static uint32_t screen_update_token;

int rt_context_load_in_progress(uint16_t id) {
	return id == 0 ? loading_context != 0 : id == loading_context;
}

static void destroy_context(uint16_t id) {
	/* the root stage forgets the children of the context, then the actors are destroyed */
	for (int i = 0; i < nstage;)
		if (stage[i].a->context == id)
			rt_stage_remove(stage[i].a);
		else
			i++;
	god_destroy_context(id);
}

/* command 0x10 "context load complete" (document parameter client 1190:2f08 → 1190:2f4a):
 * the context being read is done (1190:3366); if it is the context of the screen being
 * branched to, that screen becomes current, ScreenEntry (0x11) is posted to it, the auto
 * update is enabled again and the branch ends (1190:324e). No other event is posted. */
static void context_load_complete(uint16_t id) {
	if (id == loading_context)
		loading_context = 0;
	if (loading_screen && id == god_context_for_screen(loading_screen)) {
		current_screen = loading_screen;
		if (script_trace)
			fprintf(stderr, "[%6u] screen %u loaded\n", rt_now(), current_screen);
		rt_queue_actor_event(current_screen, EV_SCREEN_ENTRY, NULL);
		loading_screen = 0;
		if (screen_update_token) {
			rt_enable_auto_update(screen_update_token);
			screen_update_token = 0;
		}
	}
}

/* 1190:317c: ScreenExit (0x1b) goes to the current screen at once (vtable +0x1c, not through
 * the queue), then its context is destroyed (1190:45ae) */
static void blow_away_current_screen(void) {
	if (!current_screen)
		return;
	uint16_t ctx = god_context_for_screen(current_screen);
	if (ctx) {
		MsActor *s = god_actor(current_screen);
		if (s) {
			rt_trace_event(s, EV_SCREEN_EXIT, NULL);
			actor_on_event(s, EV_SCREEN_EXIT, NULL);
		}
		destroy_context(ctx);
	}
	current_screen = 0;
}

/* ScreenBranch (event 0x12 of the document, 1198:7f40): ignored while a context is being read
 * (+0x14); then 1190:3112: ignored while another branch is pending (+0x10) or when the screen
 * has no context (1190:337c returns 0, no error); otherwise the screen becomes pending, the
 * auto update is disabled if asked, the current screen is blown away and the context is read
 * (1190:32fc: only if not loaded and nothing else is being read; its stream takes the CD
 * stream device, 1190:301c). */
static void branch_to_screen(uint16_t screen, int disable_auto_update) {
	if (loading_context != 0 || loading_screen != 0)
		return;
	uint16_t ctx = god_context_for_screen(screen);
	if (!ctx) {
		ms_warn("screen %u has no context: branch ignored", screen);
		return;
	}
	loading_screen = screen;
	if (script_trace)
		fprintf(stderr, "[%6u] branch to screen %u\n", rt_now(), screen);
	if (disable_auto_update)
		screen_update_token = rt_disable_auto_update();
	blow_away_current_screen();
	if (loading_context == 0 && !ms_god.contexts[ctx]) {
		const MsContextRef *r = god_ctxref(ctx);
		if (r) {
			loading_context = ctx;
			if (script_trace)
				fprintf(stderr, "[%6u] loading context %u\n", rt_now(), ctx);
			god_read_stream(r->stream_id);
		}
	}
}

/* ===================================================================== mouse */

enum {
	F_DOWN = 0x01,
	F_UP = 0x02,
	F_MOVED = 0x04,
	F_ENTER = 0x08,
	F_EXIT = 0x10,
	F_OUT_OF_FOCUS = 0x40
};

#define ROOT ((MsActor *)&stage) /* the root stage as a mouse target */

typedef struct {
	MsActor *down, *up, *moved, *exit, *enter, *out_of_focus;
} MouseState;

static int mouse_x, mouse_y;
static MsActor *mouse_inside, *mouse_down_actor;

void rt_mouse_pos(int *x, int *y) {
	*x = mouse_x;
	*y = mouse_y;
}

MsActor *rt_mouse_inside_hotspot(void) {
	return mouse_inside;
}

MsActor *rt_mouse_down_hotspot(void) {
	return mouse_down_actor;
}

void rt_set_mouse_inside_hotspot(MsActor *a) {
	mouse_inside = a;
}

void rt_set_mouse_down_hotspot(MsActor *a) {
	mouse_down_actor = a;
}

void rt_invalidate_mouse(void) {
	queue_mouse(MOUSE_ENTER_EXIT, mouse_x, mouse_y);
}

static uint16_t find_mouse_targets(int x, int y, uint16_t mask, MouseState *st) {
	uint16_t result = 0;
	memset(st, 0, sizeof(*st));
	/* from the topmost child (lowest z) down */
	for (int i = nstage - 1; i >= 0 && mask; i--) {
		MsActor *a = stage[i].a;
		uint16_t r = 0;
		if (a->type == ACTOR_STALKING_ZAZU) {
			if (!zazu_is_active(a))
				continue;
			if (zazu_mouse_interesting(a, x, y)) {
				if (mask & F_DOWN) {
					st->down = a;
					r |= F_DOWN;
				}
				if (mask & F_ENTER) {
					st->enter = a;
					r |= F_ENTER;
				}
				if (mask & F_MOVED) {
					st->moved = a;
					r |= F_MOVED;
				}
			}
			if (zazu_mouse_inside(a) && (mask & F_EXIT)) {
				st->exit = a;
				r |= F_EXIT;
			}
			if (zazu_mouse_is_down(a) && (mask & F_UP)) {
				st->up = a;
				r |= F_UP;
			}
			mask &= (uint16_t)~r;
			result |= r;
			continue;
		}
		/* LIONKING.EXE 1190:c264 (hotspot vtable +0x2c): an inactive hotspot, or one whose
		 * loading has not completed (actor +0x1a, set by 1190:27e8), takes no mouse events */
		if (a->type != ACTOR_HOTSPOT || !hotspot_is_active(a) || !a->loaded)
			continue;
		if (hotspot_in_bounds(a, x, y)) {
			if (mask & F_DOWN) {
				st->down = a;
				r |= F_DOWN;
			}
			if (mask & F_ENTER) {
				st->enter = a;
				r |= F_ENTER;
			}
			if (mask & F_MOVED) {
				st->moved = a;
				r |= F_MOVED;
			}
		}
		if (a == mouse_inside && (mask & F_EXIT)) {
			st->exit = a;
			r |= F_EXIT;
		}
		if (a == mouse_down_actor && (mask & F_UP)) {
			st->up = a;
			r |= F_UP;
		}
		mask &= (uint16_t)~r;
		result |= r;
	}
	if (mask & F_ENTER) {
		st->enter = ROOT;
		result |= F_ENTER;
	}
	if (mask & F_OUT_OF_FOCUS) {
		st->out_of_focus = ROOT;
		result |= F_OUT_OF_FOCUS;
	}
	return result;
}

static void hotspot_event(MsActor *a, int ev) {
	if (a->type == ACTOR_STALKING_ZAZU) {
		rt_trace_event(a, ev, NULL);
		zazu_mouse_event(a, ev);
		return;
	}
	if (!hotspot_is_active(a)) {
		ms_warn("hotspot %u: event 0x%x while inactive", a->id, ev);
		return;
	}
	switch (ev) {
	case EV_MOUSE_DOWN:
		mouse_down_actor = a;
		break;
	case EV_MOUSE_UP:
		mouse_down_actor = NULL;
		break;
	case EV_MOUSE_ENTERED:
		mouse_inside = a;
		if (hotspot_cursor(a))
			rt_cursor_set_temporary(hotspot_cursor(a));
		else
			rt_cursor_unset_temporary();
		break;
	case EV_MOUSE_EXITED:
		mouse_inside = NULL;
		break;
	}
	rt_trace_event(a, ev, NULL);
	actor_run_response(a, ev, NULL);
}

static void send_enter_exit(uint16_t flags, MouseState *st) {
	if (st->enter == st->exit)
		return;
	if ((flags & F_EXIT) && st->exit && st->exit != ROOT)
		hotspot_event(st->exit, EV_MOUSE_EXITED);
	if (flags & F_ENTER) {
		if (st->enter == ROOT)
			rt_cursor_unset_temporary();
		else if (st->enter)
			hotspot_event(st->enter, EV_MOUSE_ENTERED);
	}
}

static void handle_mouse(int type, int x, int y) {
	MouseState st;
	uint16_t f;
	switch (type) {
	case MOUSE_DOWN:
		f = find_mouse_targets(x, y, F_DOWN, &st);
		if (f & F_DOWN)
			hotspot_event(st.down, EV_MOUSE_DOWN);
		break;
	case MOUSE_UP:
		f = find_mouse_targets(x, y, F_UP, &st);
		if (f & F_UP)
			hotspot_event(st.up, EV_MOUSE_UP);
		break;
	case MOUSE_MOVED:
		f = find_mouse_targets(x, y, F_ENTER | F_EXIT | F_MOVED, &st);
		send_enter_exit(f, &st);
		if ((f & F_MOVED) && st.moved && god_actor(st.moved->id) == st.moved)
			hotspot_event(st.moved, EV_MOUSE_MOVED);
		break;
	case MOUSE_ENTER_EXIT:
		f = find_mouse_targets(x, y, F_ENTER | F_EXIT, &st);
		send_enter_exit(f, &st);
		break;
	case MOUSE_OUT_OF_FOCUS:
		f = find_mouse_targets(x, y, F_EXIT | F_OUT_OF_FOCUS, &st);
		if ((f & F_EXIT) && st.exit && st.exit != ROOT)
			hotspot_event(st.exit, EV_MOUSE_EXITED);
		if (f & F_OUT_OF_FOCUS)
			rt_cursor_unset_temporary();
		break;
	}
}

/* Keyboard events go to the first active, loaded hotspot (frontmost first) with a KeyDown
 * response for exactly that character code (LIONKING.EXE 1190:c490 tests the response with
 * 1190:36d0, an exact match of event and argument: there is no "any key" response; 1190:c520
 * runs it with the code as a float). */
static void handle_key(int code) {
	MsValue key = val_float(code);
	/* the stage asks its children from the first of its list (1198:a006, iterator 1198:f7f0):
	 * the list is sorted by ascending z (comparator 1198:9784), so the frontmost child is asked
	 * first, as for the mouse; drawing walks the list backwards (1198:9d0e, 1198:f836) */
	for (int i = nstage - 1; i >= 0; i--) {
		MsActor *a = stage[i].a;
		if (a->type != ACTOR_HOTSPOT || !hotspot_is_active(a) || !a->loaded)
			continue;
		if (actor_has_response(a, EV_KEY_DOWN, &key)) {
			rt_trace_event(a, EV_KEY_DOWN, &key);
			actor_run_response(a, EV_KEY_DOWN, &key);
			return;
		}
	}
}

/* ==================================================================== timers */

static uint8_t alarm_queued[MS_MAX_ID];

/* Like the original timer service (LIONKING.EXE 1190:01a0, called first in every loop
 * iteration by 1188:fb70): timers are kept sorted by expiry time and every timer with
 * time <= now posts its event, earliest first; the first one not yet expired ends the scan.
 * Equal times keep registration order (serial number, comparator 1198:21e0). The events are
 * posted before the queue is dispatched in the same pass, so a timer posts once. */
static int alarm_cmp(const void *x, const void *y) {
	const MsActor *a = *(MsActor *const *)x, *b = *(MsActor *const *)y;
	if (a->alarm_at != b->alarm_at)
		return a->alarm_at < b->alarm_at ? -1 : 1;
	return a->alarm_serial < b->alarm_serial ? -1 : a->alarm_serial > b->alarm_serial;
}

/* ms to the next timer (at most 10): how long an idle pass may wait */
static uint32_t ms_to_next_alarm(void) {
	uint32_t now = rt_now(), next = 10;
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsActor *a = ms_god.actors[i];
		if (a && a->alarm_on && !alarm_queued[i]) {
			if (now >= a->alarm_at)
				return 0;
			if (a->alarm_at - now < next)
				next = a->alarm_at - now;
		}
	}
	return next;
}

static void queue_expired_alarms(void) {
	static MsActor *expired[MS_MAX_ID];
	int n = 0;
	uint32_t now = rt_now();
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsActor *a = ms_god.actors[i];
		if (a && a->alarm_on && now >= a->alarm_at && !alarm_queued[i])
			expired[n++] = a;
	}
	if (n > 1)
		qsort(expired, (size_t)n, sizeof(expired[0]), alarm_cmp);
	for (int k = 0; k < n; k++) {
		QEvent e;
		memset(&e, 0, sizeof(e));
		e.kind = Q_ALARM;
		e.actor = expired[k]->id;
		alarm_queued[e.actor] = 1;
		queue(e);
	}
}

/* ================================================================= dispatch */

static void dispatch(QEvent *e) {
	switch (e->kind) {
	case Q_ACTOR: {
		MsActor *a = god_actor(e->actor);
		if (!a)
			break;
		if (e->actor == DOCUMENT_ID && a->type == ACTOR_DOCUMENT)
			break; /* the document has only ScreenBranch (Q_BRANCH); others: base, nothing */
		rt_trace_event(a, e->event, &e->arg);
		actor_on_event(a, e->event, &e->arg);
		break;
	}
	case Q_BRANCH:
		branch_to_screen(e->screen, e->flag);
		break;
	case Q_ENABLE_AUTO_UPDATE:
		rt_enable_auto_update(e->token);
		break;
	case Q_ALARM: {
		alarm_queued[e->actor] = 0;
		MsActor *a = god_actor(e->actor);
		if (a && a->alarm_on) {
			a->alarm_on = 0;
			if (a->vt->timer)
				a->vt->timer(a);
		}
		break;
	}
	case Q_MOUSE:
		handle_mouse(e->event, e->x, e->y);
		break;
	case Q_KEY:
		handle_key(e->event);
		break;
	}
	val_free(&e->arg);
}

static int dispatch_events(void) {
	if (nincoming == 0)
		return 0;
	/* events generated while dispatching wait for the next frame */
	QEvent *batch = incoming;
	int n = nincoming;
	incoming = NULL;
	nincoming = capincoming = 0;
	for (int i = 0; i < n; i++)
		dispatch(&batch[i]);
	free(batch);
	return 1;
}

/* ===================================================================== hooks */

static void hook_actor_loaded(MsActor *a) {
	if (a->vt->spatial && a->stage_id == ROOT_STAGE_ID)
		stage_add(a);
	else if (a->vt->spatial && a->stage_id)
		ms_warn("actor %u: stage %u not supported (only the root stage)", a->id, a->stage_id);
}

static void hook_transition(MsValue *args, int nargs) {
	video_effect_transition(args, nargs);
}

/* =================================================================== run */

typedef struct {
	uint32_t at;
	int kind; /* 0 click, 1 screenshot, 2 key, 3 mouse down, 4 mouse up, 5 mouse move */
	int x, y;
	char path[256];
	int done;
} Scripted;

#define MAX_SCRIPTED 1024 /* each --click takes two entries (its release is added later) */
static Scripted scripted[MAX_SCRIPTED];
static int nscripted;
static uint32_t quit_after;

static void add_scripted(Scripted s) {
	if (nscripted < MAX_SCRIPTED)
		scripted[nscripted++] = s;
	else
		ms_warn("too many scripted inputs (max %d): input at %u ms dropped", MAX_SCRIPTED, s.at);
}

void rt_add_scripted_click(uint32_t at, int x, int y) {
	add_scripted((Scripted){at, 0, x, y, "", 0});
}

void rt_add_scripted_shot(uint32_t at, const char *path) {
	Scripted s = {at, 1, 0, 0, "", 0};
	snprintf(s.path, sizeof(s.path), "%s", path);
	add_scripted(s);
}

void rt_add_scripted_key(uint32_t at, int code) {
	add_scripted((Scripted){at, 2, code, 0, "", 0});
}

void rt_add_scripted_mouse(uint32_t at, int type, int x, int y) {
	add_scripted((Scripted){at, 3 + type, x, y, "", 0});
}

void rt_set_quit_after(uint32_t ms) {
	quit_after = ms;
}

/* Key codes as the original makes them (frame message map at 1008:0000):
 * - WM_KEYDOWN (1188:0474) only for the arrows: VK_LEFT 0x100, VK_RIGHT 0x101, VK_UP 0x102,
 *   VK_DOWN 0x103; every other key is left to WM_CHAR;
 * - WM_CHAR (1188:03d4): the character code itself (control characters included: Backspace
 *   8, Tab 9, Enter 13, Esc 27, Ctrl+letter 1..26). Printable characters come from
 *   SDL_TEXTINPUT (so the case and the keyboard layout are right, like WM_CHAR), the control
 *   characters from here. */
static int key_code(SDL_Keycode k, Uint16 mod) {
	switch (k) {
	case SDLK_LEFT:
		return 0x100;
	case SDLK_RIGHT:
		return 0x101;
	case SDLK_UP:
		return 0x102;
	case SDLK_DOWN:
		return 0x103;
	case SDLK_BACKSPACE:
		return 0x08;
	case SDLK_TAB:
		return 0x09;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		return 13;
	case SDLK_ESCAPE:
		return 27;
	}
	if ((mod & KMOD_CTRL) && !(mod & KMOD_ALT) && k >= SDLK_a && k <= SDLK_z)
		return (int)(k - SDLK_a + 1);
	return 0;
}

static void run_scripted(void) {
	uint32_t now = rt_now();
	for (int i = 0; i < nscripted; i++) {
		Scripted *s = &scripted[i];
		if (s->done || now < s->at)
			continue;
		s->done = 1;
		if (s->kind == 0) {
			mouse_x = s->x;
			mouse_y = s->y;
			queue_mouse(MOUSE_MOVED, s->x, s->y);
			queue_mouse(MOUSE_DOWN, s->x, s->y);
			/* the button is released 100 ms later, like a real click: actions started by a
			 * MouseDown and stopped by the MouseUp (timers of the page chooser arrows) need
			 * at least one frame in between */
			rt_add_scripted_mouse(now + 100, 1, s->x, s->y);
		} else if (s->kind == 1) {
			if (!video_save_bmp(s->path))
				ms_warn("cannot write %s", s->path);
			else
				fprintf(stderr, "[%6u] screenshot %s\n", now, s->path);
		} else if (s->kind == 2) {
			queue_key(s->x);
		} else {
			mouse_x = s->x;
			mouse_y = s->y;
			queue_mouse(MOUSE_MOVED, s->x, s->y);
			if (s->kind == 3)
				queue_mouse(MOUSE_DOWN, s->x, s->y);
			else if (s->kind == 4)
				queue_mouse(MOUSE_UP, s->x, s->y);
		}
	}
	if (quit_after && now >= quit_after)
		running = 0;
}

/* ------------------------------------------------------------ window messages */

/* The SDL events play the part of the Windows message queue. Input messages wait in a FIFO
 * and are turned into engine events by pump_input(), at most one engine event per pass of
 * the loop (see there). Consecutive mouse moves are merged, as Windows does with
 * WM_MOUSEMOVE (it is generated from the latest position when the queue is read). */
enum { IN_MOVE, IN_LEAVE, IN_DOWN, IN_UP, IN_KEY };
typedef struct {
	int type, x, y, code;
} InputMsg;

#define MAX_INPUT 256
static InputMsg inq[MAX_INPUT];
static int inq_head, inq_count;

static void input_push(InputMsg m) {
	if (m.type == IN_MOVE && inq_count > 0) {
		InputMsg *last = &inq[(inq_head + inq_count - 1) % MAX_INPUT];
		if (last->type == IN_MOVE) {
			*last = m;
			return;
		}
	}
	if (inq_count == MAX_INPUT)
		return; /* a full queue drops input, like a full Windows message queue */
	inq[(inq_head + inq_count++) % MAX_INPUT] = m;
}

static int last_move_x = -1, last_move_y = -1, mouse_outside;

/* One Windows input message (view message map at 1008:00a0). */
static void input_message(const InputMsg *m) {
	switch (m->type) {
	case IN_MOVE:
		/* WM_MOUSEMOVE (1188:0abc): nothing unless the position changed; inside the client
		 * area MouseMoved (subtype 8), outside (the view keeps the capture) one "out of
		 * focus" (subtype 0xc) until the mouse comes back */
		if (m->x == last_move_x && m->y == last_move_y)
			break;
		last_move_x = m->x;
		last_move_y = m->y;
		mouse_x = m->x;
		mouse_y = m->y;
		queue_mouse(MOUSE_MOVED, m->x, m->y);
		mouse_outside = 0;
		break;
	case IN_LEAVE:
		last_move_x = last_move_y = -1;
		if (!mouse_outside) {
			mouse_outside = 1;
			queue_mouse(MOUSE_OUT_OF_FOCUS, mouse_x, mouse_y);
		}
		break;
	case IN_DOWN:
	case IN_UP:
		/* WM_LBUTTONDOWN/DBLCLK, WM_MBUTTONDOWN/DBLCLK and WM_RBUTTONDOWN/DBLCLK all post
		 * MouseDown (subtype 6), the three button-up messages MouseUp (7), at the point of
		 * the message (1188:099c ... 1188:0bbc) */
		mouse_x = m->x;
		mouse_y = m->y;
		queue_mouse(m->type == IN_DOWN ? MOUSE_DOWN : MOUSE_UP, m->x, m->y);
		break;
	case IN_KEY:
		queue_key(m->code);
		if (m->code == 0x16) {
			/* Ctrl+V: after posting the key, WM_CHAR (1188:0442) opens the modal dialog 1 of
			 * the executable (DialogBox, dialog procedure 1188:5874): the "About" box, filled
			 * at run time from the executable's strings and version (cursors.c) */
			char title[256], text[2048], button[64];
			if (cursors_about_box(title, sizeof(title), text, sizeof(text), button, sizeof(button))) {
				const SDL_MessageBoxButtonData ok = {
					SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, button};
				const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, title, text, 1, &ok, NULL};
				int pressed;
				SDL_ShowMessageBox(&box, &pressed);
			}
		}
		break;
	}
}

/* LIONKING.EXE 1188:11e4 (platform vtable +8): Windows messages are dispatched until one of
 * them posts an engine event (event counter at queue +0x38 changes), so every pass of the
 * engine loop takes at most one mouse or keyboard input. */
static int pump_input(void) {
	int before = nincoming, any = 0;
	while (inq_count > 0 && nincoming == before) {
		InputMsg m = inq[inq_head];
		inq_head = (inq_head + 1) % MAX_INPUT;
		inq_count--;
		input_message(&m);
		any = 1;
	}
	return any;
}

/* Focus loss (WM_ACTIVATEAPP, 1188:0260 → 1188:02f6, only while the title runs, flag +0x54
 * of the title object set around the engine loop by 1198:0529/05bc): the wave device is
 * paused (waveOutPause, 1188:219a), the engine clock is paused (1190:04a2), the engine loop
 * turns into a plain message loop (1188:fcd0(1): queue +0x3c, the loop calls the modal loop
 * 1188:1260), the mouse capture is released and the window minimized
 * (ShowWindow SW_SHOWMINNOACTIVE). WM_SIZE SIZE_RESTORED (1188:051a → 1188:033c) resumes:
 * waveOutRestart (1188:2202), clock (1190:04fa), engine loop (1188:fcd0(0)), a WM_MOUSEMOVE
 * at the cursor position, the cursor set again, SetCapture. Not done in timed test runs
 * (--quit-after or scripted inputs): they must not depend on the window focus. */
static int app_paused;
extern void audio_device_pause(int pause);
extern void cursors_refresh(void);

static void app_deactivate(SDL_Window *w) {
	if (app_paused || quit_after || nscripted)
		return;
	app_paused = 1;
	audio_device_pause(1);
	clock_pause(1);
	if (w)
		SDL_MinimizeWindow(w);
}

static void app_reactivate(void) {
	if (!app_paused)
		return;
	app_paused = 0;
	audio_device_pause(0);
	clock_pause(0);
	int wx, wy, gx, gy;
	SDL_GetMouseState(&wx, &wy);
	video_window_to_game(wx, wy, &gx, &gy);
	InputMsg m = {IN_MOVE, gx, gy, 0};
	input_message(&m); /* SendMessage: handled at once */
	cursors_refresh();
}

static void poll_system_events(void) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		InputMsg m = {0, 0, 0, 0};
		switch (ev.type) {
		case SDL_QUIT:
			running = 0;
			break;
		/* With a logical size set, SDL already delivers mouse event coordinates in game
		 * (logical) pixels; converting them again misplaced every click when scaled.
		 * Stretched (no logical size) video_event_to_game converts window pixels. */
		case SDL_MOUSEMOTION: {
			int x = ev.motion.x, y = ev.motion.y;
			video_event_to_game(&x, &y);
			if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) {
				m.type = IN_LEAVE; /* moved out of the game area while captured */
			} else {
				m.type = IN_MOVE;
				m.x = x;
				m.y = y;
			}
			input_push(m);
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			if (ev.button.button != SDL_BUTTON_LEFT && ev.button.button != SDL_BUTTON_MIDDLE &&
				ev.button.button != SDL_BUTTON_RIGHT)
				break;
			int x = ev.button.x, y = ev.button.y;
			video_event_to_game(&x, &y);
			m.type = ev.type == SDL_MOUSEBUTTONDOWN ? IN_DOWN : IN_UP;
			m.x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
			m.y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
			input_push(m);
			break;
		}
		case SDL_KEYDOWN:
			if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
				video_toggle_fullscreen();
				break;
			}
			if (ev.key.keysym.sym == SDLK_F12) {
				char path[64];
				snprintf(path, sizeof(path), "shot_%u.bmp", rt_now());
				video_save_bmp(path);
				break;
			}
			m.code = key_code(ev.key.keysym.sym, ev.key.keysym.mod);
			if (m.code) {
				m.type = IN_KEY;
				input_push(m);
			}
			break;
		case SDL_TEXTINPUT: {
			/* WM_CHAR of a printable character: the code in the ANSI code page (Latin-1 here) */
			const unsigned char *t = (const unsigned char *)ev.text.text;
			while (*t) {
				int c;
				if (*t < 0x80) {
					c = *t++;
				} else if ((*t & 0xe0) == 0xc0 && (t[1] & 0xc0) == 0x80) {
					c = (*t & 0x1f) << 6 | (t[1] & 0x3f);
					t += 2;
				} else {
					t++;
					while ((*t & 0xc0) == 0x80)
						t++;
					continue;
				}
				if (c >= 32 && c < 256 && c != 127) {
					m.type = IN_KEY;
					m.code = c;
					input_push(m);
				}
			}
			break;
		}
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
				m.type = IN_LEAVE;
				input_push(m);
			} else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				app_deactivate(SDL_GetWindowFromID(ev.window.windowID));
			} else if (ev.window.event == SDL_WINDOWEVENT_RESTORED ||
				   ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				app_reactivate();
			} else if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) {
				force_flush = 1; /* WM_PAINT (1188:083a) */
			}
			break;
		}
	}
}

void rt_init(void) {
	clock_base = SDL_GetTicks();
	ms_god_hooks.context_load_complete = context_load_complete;
	ms_god_hooks.display_update_all = update_all;
	ms_god_hooks.display_update_dirty = update_dirty;
	ms_god_hooks.display_transition = hook_transition;
	ms_god_hooks.new_cursor = cursors_new;
	ms_god_hooks.dispose_cursor = cursors_dispose;
	ms_god_hooks.actor_loaded = hook_actor_loaded;
	/* the document actor (id 1) receives the branch/context methods of the scripts */
	MsActor *doc = actor_create(0, ACTOR_DOCUMENT, DOCUMENT_ID, NULL);
	ms_god.actors[DOCUMENT_ID] = doc;
}

void rt_run(const char *data_dir, uint16_t start_screen) {
	god_boot(data_dir);
	uint16_t entry = start_screen ? start_screen : ms_god.entry_screen;
	/* the end of the boot stream branches to the entry screen with the auto update disabled
	 * (stream client 1190:2e68 → 1190:3112(1, +0x16)) */
	branch_to_screen(entry, 1);
	SDL_StartTextInput(); /* SDL_TEXTINPUT gives the WM_CHAR codes */
	/* LIONKING.EXE 1188:fb70, one pass per iteration: timers (platform +0x14 → 1190:01a0),
	 * the engine queue if not empty (1188:fd14), the Windows messages if any (platform +4
	 * PeekMessage, +8 = 1188:11e4: at most one input), the services (platform +0x18 =
	 * 1188:f6b2: only playing movies register there, 1198:1f36), then the screen event
	 * (1188:fd9c → 1190:8e2e) if anything happened, else the MFC idle handler. The original
	 * never sleeps; here an idle pass waits up to the next timer (at most 10 ms). */
	while (running) {
		if (app_paused) {
			/* the engine loop runs the modal message loop (1188:1260) until reactivated */
			poll_system_events();
			SDL_Delay(10);
			continue;
		}
		queue_expired_alarms();
		int had_events = dispatch_events();
		poll_system_events();
		int had_input = pump_input();
		run_scripted();
		int sync = pre_display_sync();
		/* 1188:fb70: the screen event only if something happened and no service said skip */
		if (sync != RT_SYNC_SKIP &&
		    (had_events || had_input || sync == RT_SYNC_CHANGED || ndirty > 0 || force_flush))
			auto_update_and_flush();
		else
			present_screen();
		audio_service();
		if (nincoming == 0 && inq_count == 0) {
			uint32_t wait = ms_to_next_alarm();
			if (wait > 0)
				SDL_Delay(wait);
		}
	}
}
