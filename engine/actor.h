/* Actors: every object of a title (screens, images, sprites, sounds, movies, hotspots, timers,
 * paths, palettes, cursors, the Stalking Zazu minigame...). An actor is created by a control
 * command of a context, reads its header parameters, then receives the data chunks of its
 * channel(s). Each type has a small "vtable" (MsActorType) in actors_*.c. */
#ifndef MS_ACTOR_H
#define MS_ACTOR_H

#include <stdint.h>

#include "bitmap.h"
#include "datafile.h"
#include "value.h"

typedef struct MsActor MsActor;

/* A script attached to an event of an actor ("On MouseDown ... End"). */
typedef struct {
	uint16_t event;
	MsValue arg;
	uint8_t *code;
	uint32_t size;
} MsResponse;

/* Sub-channels: a movie owns three channels (header, frames, sound). */
enum { CH_MAIN = 0, CH_MOVIE_FRAMES = 1, CH_MOVIE_SOUND = 2 };

typedef struct {
	const char *name;
	size_t ext_size; /* size of the type-specific data (MsActor.x) */
	int spatial;
	void (*init)(MsActor *a);
	/* returns 1 if the parameter was consumed */
	int (*read_param)(MsActor *a, MsChunk *c, uint16_t param);
	void (*read_chunk)(MsActor *a, int sub, MsChunk *c);
	void (*load_complete)(MsActor *a);
	/* *handled = 0 to fall back to the spatial/base methods */
	MsValue (*call_method)(MsActor *a, uint32_t method, MsValue *args, int nargs, int *handled);
	void (*on_event)(MsActor *a, int event, const MsValue *arg);
	void (*timer)(MsActor *a);
	void (*draw)(MsActor *a);
	void (*destroy)(MsActor *a);
} MsActorType;

struct MsActor {
	const MsActorType *vt;
	uint16_t type, id, context;
	char *name;
	MsResponse *resp;
	int nresp;
	int loaded;
	uint32_t channel; /* main channel ident (0 = none) */

	/* timer service (one alarm per actor, like the original TimerEntry) */
	int alarm_on;
	uint32_t alarm_at;
	uint32_t alarm_serial; /* registration order: ties of alarm_at (LIONKING.EXE 1198:204a) */
	uint32_t start_time, last_processed;

	/* spatial entities */
	MsRect obox; /* bounding box as defined (moveTo changes it) */
	MsRect box;  /* adjusted (parallax/stage); equal to obox without stages */
	int z;
	double dissolve;
	int visible;
	int transparent;
	uint16_t stage_id;
	double parallax_x, parallax_y;

	void *x; /* type-specific data, vt->ext_size bytes */
};

const MsActorType *actor_type_for(int type);
const char *actor_type_name(int type);

/* Creates the actor and reads its header parameters (control command 0x11). */
MsActor *actor_create(uint16_t context, uint16_t type, uint16_t id, MsChunk *c);
void actor_destroy(MsActor *a);

/* Base parameters shared by all actors / by spatial entities; return 1 if consumed. */
int actor_read_base_param(MsActor *a, MsChunk *c, uint16_t param);
int actor_read_spatial_param(MsActor *a, MsChunk *c, uint16_t param);

/* Script responses */
MsResponse *actor_find_response(MsActor *a, int event, const MsValue *arg);
int actor_has_response(MsActor *a, int event, const MsValue *arg);
/* Runs the response for (event, arg) if there is one; arg NULL = empty value. */
void actor_run_response(MsActor *a, int event, const MsValue *arg);

/* Time-based responses (On Time x) of sounds, movies, timers */
MsResponse *actor_next_time_response(MsActor *a, uint32_t after_ms);
void actor_process_time_responses(MsActor *a);
int actor_setup_next_time_timer(MsActor *a);
void actor_trigger_remaining_time_responses(MsActor *a);

/* Spatial helpers */
MsRect actor_bbox(MsActor *a); /* type-specific for images */
void actor_move_to(MsActor *a, int x, int y);
void actor_invalidate(MsActor *a);

/* Default method handling (spatial methods, then "unimplemented" warning). */
MsValue actor_call_method(MsActor *a, uint32_t method, MsValue *args, int nargs);
void actor_on_event(MsActor *a, int event, const MsValue *arg);

/* Alarm (TimerService) */
void actor_start_alarm(MsActor *a, uint32_t delay_ms);
void actor_stop_alarm(MsActor *a);

/* Type-specific data of each actor type, defined in actors_*.c */
extern const MsActorType ms_type_screen, ms_type_image, ms_type_sound, ms_type_movie,
	ms_type_sprite, ms_type_hotspot, ms_type_timer, ms_type_path, ms_type_palette,
	ms_type_cursor, ms_type_zazu, ms_type_document, ms_type_dotgame;

#endif
