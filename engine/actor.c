#include "actor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "god.h"
#include "runtime.h"
#include "script.h"

const MsActorType *actor_type_for(int type) {
	switch (type) {
	case ACTOR_SCREEN:
		return &ms_type_screen;
	case ACTOR_IMAGE:
		return &ms_type_image;
	case ACTOR_SOUND:
		return &ms_type_sound;
	case ACTOR_MOVIE:
		return &ms_type_movie;
	case ACTOR_SPRITE:
		return &ms_type_sprite;
	case ACTOR_HOTSPOT:
		return &ms_type_hotspot;
	case ACTOR_TIMER:
		return &ms_type_timer;
	case ACTOR_PATH:
		return &ms_type_path;
	case ACTOR_PALETTE:
		return &ms_type_palette;
	case ACTOR_CURSOR:
		return &ms_type_cursor;
	case ACTOR_STALKING_ZAZU:
		return &ms_type_zazu;
	case ACTOR_DOCUMENT:
		return &ms_type_document;
	case ACTOR_DOT_GAME:
		return &ms_type_dotgame;
	}
	return NULL;
}

const char *actor_type_name(int type) {
	const MsActorType *t = actor_type_for(type);
	if (t)
		return t->name;
	switch (type) {
	case ACTOR_STAGE:
		return "Stage";
	case ACTOR_TEXT:
		return "Text";
	case ACTOR_FONT:
		return "Font";
	case ACTOR_CAMERA:
		return "Camera";
	case ACTOR_CANVAS:
		return "Canvas";
	case ACTOR_DISK_IMAGE:
		return "DiskImage";
	case ACTOR_PRINTER:
		return "Printer";
	}
	return "?";
}

MsActor *actor_create(uint16_t context, uint16_t type, uint16_t id, MsChunk *c) {
	const MsActorType *vt = actor_type_for(type);
	if (!vt)
		ms_fatal("actor %u: type 0x%x (%s) not supported", id, type, actor_type_name(type));
	MsActor *a = calloc(1, sizeof(MsActor));
	a->vt = vt;
	a->type = type;
	a->id = id;
	a->context = context;
	a->dissolve = 1.0;
	if (vt->ext_size)
		a->x = calloc(1, vt->ext_size);
	if (vt->init)
		vt->init(a);
	if (c) {
		for (;;) {
			uint16_t p = ms_t_u16(c);
			if (p == AH_END)
				break;
			int done = vt->read_param ? vt->read_param(a, c, p) : 0;
			if (!done && vt->spatial)
				done = actor_read_spatial_param(a, c, p);
			if (!done)
				done = actor_read_base_param(a, c, p);
			if (!done)
				ms_fatal("actor %u (%s): unknown parameter 0x%x @0x%zx", id, vt->name, p, c->pos);
		}
	}
	return a;
}

void actor_destroy(MsActor *a) {
	if (!a)
		return;
	actor_stop_alarm(a);
	if (a->vt->destroy)
		a->vt->destroy(a);
	if (a->channel)
		god_unregister_channel(a->channel);
	if (rt_mouse_inside_hotspot() == a)
		rt_set_mouse_inside_hotspot(NULL);
	if (rt_mouse_down_hotspot() == a)
		rt_set_mouse_down_hotspot(NULL);
	rt_unregister_sync(a);
	rt_stage_remove(a);
	for (int i = 0; i < a->nresp; i++) {
		val_free(&a->resp[i].arg);
		free(a->resp[i].code);
	}
	free(a->resp);
	free(a->name);
	free(a->x);
	free(a);
}

int actor_read_base_param(MsActor *a, MsChunk *c, uint16_t param) {
	switch (param) {
	case AH_SCRIPT_RESPONSE: {
		a->resp = realloc(a->resp, sizeof(MsResponse) * (size_t)(a->nresp + 1));
		MsResponse *r = &a->resp[a->nresp++];
		r->event = ms_t_u16(c);
		r->arg = val_read(c);
		r->size = ms_t_u32(c);
		r->code = malloc(r->size ? r->size : 1);
		ms_read(c, r->code, r->size);
		ms_god_stats.responses++;
		ms_god_stats.code_bytes += r->size;
		return 1;
	}
	case AH_ACTOR_NAME:
		free(a->name);
		a->name = ms_t_string(c);
		return 1;
	}
	return 0;
}

int actor_read_spatial_param(MsActor *a, MsChunk *c, uint16_t param) {
	switch (param) {
	case AH_BOUNDING_BOX:
		a->obox = ms_t_rect(c);
		a->box = a->obox;
		return 1;
	case AH_DISSOLVE_FACTOR: {
		double d = ms_t_double(c);
		a->dissolve = d < 0 ? 0 : d > 1 ? 1 : d;
		return 1;
	}
	case AH_Z_INDEX:
		a->z = ms_t_gunit(c);
		return 1;
	case AH_TRANSPARENCY:
		a->transparent = ms_t_u8(c) != 0;
		return 1;
	case AH_CHILD_ACTOR_ID:
		a->stage_id = ms_t_u16(c);
		return 1;
	case AH_SCALE_X_AND_Y:
		a->parallax_x = a->parallax_y = ms_t_double(c);
		return 1;
	case AH_SCALE_X:
		a->parallax_x = ms_t_double(c);
		return 1;
	case AH_SCALE_Y:
		a->parallax_y = ms_t_double(c);
		return 1;
	}
	return 0;
}

/* ---- script responses ---- */

MsResponse *actor_find_response(MsActor *a, int event, const MsValue *arg) {
	MsValue empty = val_empty();
	if (!arg)
		arg = &empty;
	for (int i = 0; i < a->nresp; i++) {
		MsResponse *r = &a->resp[i];
		if (r->event != event)
			continue;
		if (r->arg.type != arg->type)
			continue;
		if (val_compare(OP_EQUALS, arg, &r->arg))
			return r;
	}
	return NULL;
}

int actor_has_response(MsActor *a, int event, const MsValue *arg) {
	return actor_find_response(a, event, arg) != NULL;
}

void actor_run_response(MsActor *a, int event, const MsValue *arg) {
	MsResponse *r = actor_find_response(a, event, arg);
	if (r) {
		uint16_t id = a->id;
		MsValue ret = rt_run_code(id, r->code, r->size, NULL, 0);
		val_free(&ret);
	}
}

/* Time responses ("On Time 1.5") are kept in file order, which is time order. */
MsResponse *actor_next_time_response(MsActor *a, uint32_t after_ms) {
	for (int i = 0; i < a->nresp; i++) {
		MsResponse *r = &a->resp[i];
		if (r->event != EV_TIMER)
			continue;
		uint32_t t = (uint32_t)(val_as_float(&r->arg) * 1000);
		if (t >= after_ms)
			return r;
	}
	return NULL;
}

void actor_process_time_responses(MsActor *a) {
	uint32_t elapsed = rt_now() - a->start_time;
	MsResponse *r = actor_next_time_response(a, a->last_processed);
	while (r) {
		uint32_t t = (uint32_t)(val_as_float(&r->arg) * 1000);
		if (t > elapsed)
			break;
		uint16_t id = a->id;
		MsValue ret = rt_run_code(id, r->code, r->size, NULL, 0);
		val_free(&ret);
		/* the script may have destroyed this actor */
		if (god_actor(id) != a)
			return;
		a->last_processed = t + 1;
		r = actor_next_time_response(a, a->last_processed);
	}
}

int actor_setup_next_time_timer(MsActor *a) {
	MsResponse *r = actor_next_time_response(a, a->last_processed);
	if (!r)
		return 0;
	uint32_t t = (uint32_t)(val_as_float(&r->arg) * 1000);
	uint32_t elapsed = rt_now() - a->start_time;
	actor_start_alarm(a, t > elapsed ? t - elapsed : 0);
	return 1;
}

void actor_trigger_remaining_time_responses(MsActor *a) {
	MsResponse *r = actor_next_time_response(a, a->last_processed);
	while (r) {
		uint32_t t = (uint32_t)(val_as_float(&r->arg) * 1000);
		a->last_processed = t + 1;
		uint16_t id = a->id;
		MsValue ret = rt_run_code(id, r->code, r->size, NULL, 0);
		val_free(&ret);
		if (god_actor(id) != a)
			return;
		r = actor_next_time_response(a, a->last_processed);
	}
}

/* ---- alarms ---- */

/* LIONKING.EXE 1190:008e: (re)registering a timer first removes the client's entry
 * (1190:010a), then inserts a new one at now + delay, sorted by expiry time and, for equal
 * times, by a serial number taken from a global counter at every registration (1198:204a,
 * DS:03AC; comparator 1198:21e0 used by 1198:2e6a). */
void actor_start_alarm(MsActor *a, uint32_t delay_ms) {
	static uint32_t serial;
	a->alarm_on = 1;
	a->alarm_at = rt_now() + delay_ms;
	a->alarm_serial = ++serial;
}

void actor_stop_alarm(MsActor *a) {
	a->alarm_on = 0;
}

/* ---- spatial ---- */

MsRect actor_bbox(MsActor *a) {
	extern MsRect image_bbox(MsActor * a);
	if (a->type == ACTOR_IMAGE)
		return image_bbox(a);
	return a->box;
}

void actor_invalidate(MsActor *a) {
	rt_invalidate(actor_bbox(a));
}

void actor_move_to(MsActor *a, int x, int y) {
	if (a->obox.x == x && a->obox.y == y)
		return;
	if (a->visible)
		actor_invalidate(a);
	a->obox.x = (int16_t)x;
	a->obox.y = (int16_t)y;
	a->box = a->obox;
	if (a->visible)
		actor_invalidate(a);
	if (a->type == ACTOR_HOTSPOT)
		rt_invalidate_mouse();
}

/* Numeric argument i of a method as LIONKING.EXE reads it: float() (0.0 for anything that is
 * not a float, time included: 1190:4dc0), then _ftol (1180:1202), 16 bits. A missing argument
 * reads the extra empty value of the argument array (1198:e72e allocates n + 1): 0. */
static int16_t arg_int(MsValue *args, int nargs, int i) {
	return i < nargs ? (int16_t)ms_ftol(val_float_of(&args[i])) : 0;
}

/* setDissolve (0xf1) of Image (1190:c8fe), Sprite (1068:0d44) and Movie (1190:f998): nothing
 * without an argument; float() clamped to 0..1; redrawn only if it changes */
static void set_dissolve(MsActor *a, MsValue *args, int nargs) {
	if (nargs == 0)
		return;
	double d = val_float_of(&args[0]);
	d = d < 0.0 ? 0.0 : d > 1.0 ? 1.0 : d;
	if (d != a->dissolve) {
		a->dissolve = d;
		actor_invalidate(a);
	}
}

/* Method dispatch of LIONKING.EXE: the method handler of the class (vtable +4) handles its own
 * methods and passes the others up: Image, Hotspot, Sprite, Movie, Dot game and Stalking Zazu
 * to the spatial entity (1190:93a4), every class finally to the actor base (1190:27f8), which
 * does nothing. So an unknown method is silently ignored and gives an empty value. The
 * per-class lists are in docs\ORIGINAL_SCRIPT.md. */
MsValue actor_call_method(MsActor *a, uint32_t m, MsValue *args, int nargs) {
	/* 1190:f998: a movie ignores every method until it is loaded (+0x1a) */
	if (a->type == ACTOR_MOVIE && !a->loaded && !script_extensions)
		return val_empty();
	if (a->vt->call_method) {
		int handled = 1;
		MsValue r = a->vt->call_method(a, m, args, nargs, &handled);
		if (handled)
			return r;
	}
	if (m == M_SET_DISSOLVE_FACTOR &&
		(a->type == ACTOR_IMAGE || a->type == ACTOR_SPRITE || a->type == ACTOR_MOVIE)) {
		set_dissolve(a, args, nargs);
		return val_empty();
	}
	if (a->vt->spatial) {
		/* 1190:93a4 */
		switch (m) {
		case M_SPATIAL_MOVE_TO: /* 1190:9672 */
			actor_move_to(a, arg_int(args, nargs, 0), arg_int(args, nargs, 1));
			return val_empty();
		case M_SPATIAL_MOVE_TO_BY_OFFSET: /* 1188:8f9a then 1190:9672 */
			actor_move_to(a, a->box.x + arg_int(args, nargs, 0), a->box.y + arg_int(args, nargs, 1));
			return val_empty();
		case M_SPATIAL_Z_MOVE_TO: { /* 1190:982a: only if z changes */
			int z = arg_int(args, nargs, 0);
			if (a->z != z) {
				a->z = z;
				rt_stage_resort();
				actor_invalidate(a);
				if (a->type == ACTOR_HOTSPOT)
					rt_invalidate_mouse();
			}
			return val_empty();
		}
		case M_SPATIAL_CENTER_MOVE_TO: /* 1190:9734 */
			actor_move_to(a, arg_int(args, nargs, 0) - a->box.w / 2, arg_int(args, nargs, 1) - a->box.h / 2);
			return val_empty();
		case M_GET_LEFT_X:
			return val_float(a->box.x);
		case M_GET_TOP_Y:
			return val_float(a->box.y);
		case M_GET_WIDTH:
			return val_float(a->box.w);
		case M_GET_HEIGHT:
			return val_float(a->box.h);
		case M_GET_CENTER_X:
			return val_float(a->box.x + a->box.w / 2);
		case M_GET_CENTER_Y:
			return val_float(a->box.y + a->box.h / 2);
		case M_GET_Z_COORDINATE:
			return val_float(a->z);
		case M_IS_POINT_INSIDE: {
			/* 1190:6c02 on the stored rectangle +0x1c (bounding box parameter 0x1c, read by
			 * 1190:988c), not the drawn image: left <= x < left + w, top <= y < top + h, the
			 * same rectangle as the getters for every spatial class (Image, Sprite, Movie...).
			 * No argument count check. */
			MsRect b = a->box;
			int x = arg_int(args, nargs, 0), y = arg_int(args, nargs, 1);
			return val_bool(x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
		}
		}
		/* not in LIONKING.EXE (later engines): extensions only */
		if (script_extensions) {
			switch (m) {
			case M_SET_DISSOLVE_FACTOR:
				set_dissolve(a, args, nargs);
				return val_empty();
			case M_GET_MOUSE_X_OFFSET: {
				int x, y;
				rt_mouse_pos(&x, &y);
				return val_float(x - a->obox.x);
			}
			case M_GET_MOUSE_Y_OFFSET: {
				int x, y;
				rt_mouse_pos(&x, &y);
				return val_float(y - a->obox.y);
			}
			case M_IS_VISIBLE:
				return val_bool(a->type == ACTOR_HOTSPOT ? 0 : a->visible);
			}
		}
	}
	/* 1190:27f8: the actor base does nothing */
	if (script_trace)
		fprintf(stderr, "[%u] actor %u (%s): method 0x%x ignored (no handler, as in LIONKING.EXE)\n", rt_now(),
			a->id, a->vt->name, m);
	return val_empty();
}

void actor_on_event(MsActor *a, int event, const MsValue *arg) {
	if (a->vt->on_event)
		a->vt->on_event(a, event, arg);
	else
		ms_warn("actor %u (%s): no handler for event 0x%x", a->id, a->vt->name, event);
}
