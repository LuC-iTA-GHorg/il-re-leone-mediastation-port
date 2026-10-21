/* The title ("ImtGod" in the original): what BOOT.STM declares (files, streams, contexts,
 * screens), the loaded contexts with their actors, global variables and functions, and the
 * channel clients that receive the data chunks of the streams. */
#ifndef MS_GOD_H
#define MS_GOD_H

#include <stdint.h>

#include "actor.h"
#include "datafile.h"
#include "value.h"

#define MS_MAX_ID 65536

typedef struct {
	uint16_t id;
	uint16_t location;
	char *name;
} MsFileInfo;

typedef struct {
	uint16_t actor_id; /* = stream id */
	uint16_t file_id;
	uint32_t offset;
} MsStreamInfo;

typedef struct {
	uint16_t context_id;
	uint16_t stream_id;
	char *name;
	uint16_t parents[16];
	int nparents;
} MsContextRef;

typedef struct {
	uint16_t screen_id;
	uint16_t context_id;
} MsScreenRef;

typedef struct {
	uint16_t id;
	char *name;
	int loaded;
	uint16_t *vars; /* ids of the variables created by this context */
	int nvars, capvars;
} MsContext;

typedef struct {
	uint16_t id, context;
	uint8_t *code;
	uint32_t size;
} MsFunction;

typedef struct {
	char *name;
	uint32_t value;
} MsParamToken;

typedef struct {
	char data_dir[512]; /* folder with BOOT.STM and the *.CXT files */

	/* BOOT.STM */
	char *title, *engine_info, *source;
	uint16_t ver_major, ver_minor, ver_patch;
	uint16_t entry_screen;
	uint16_t function_table_size;
	MsFileInfo *files;
	int nfiles;
	MsStreamInfo *streams;
	int nstreams;
	MsContextRef *ctxrefs;
	int nctxrefs;
	MsScreenRef *screenrefs;
	int nscreenrefs;
	MsParamToken *tokens;
	int ntokens;
	uint16_t cursor_base, cursor_max;

	/* runtime state */
	MsContext *contexts[MS_MAX_ID];
	MsActor *actors[MS_MAX_ID];
	MsValue *vars[MS_MAX_ID];
	uint16_t var_context[MS_MAX_ID];
	MsFunction *functions[MS_MAX_ID];

	uint8_t palette[768];      /* registered palette (display manager) */
	int have_palette;
	double default_transition_time;
	int allow_multiple_sounds, allow_multiple_streams;
} MsGod;

extern MsGod ms_god;

/* Callbacks of the runtime (document.c) for the commands of the streams; the loader works
 * without them (msdump). */
typedef struct {
	void (*context_load_complete)(uint16_t context_id);
	void (*display_update_all)(void);
	void (*display_update_dirty)(void);
	void (*display_transition)(MsValue *args, int nargs);
	void (*new_cursor)(uint16_t id, int type, uint16_t platform_id, const char *resource);
	void (*dispose_cursor)(uint16_t id);
	void (*actor_loaded)(MsActor *a);
} MsGodHooks;
extern MsGodHooks ms_god_hooks;

/* Reads BOOT.STM from dir. */
void god_boot(const char *data_dir);
/* Reads a whole stream (one subfile) and dispatches its chunks: "igod" to the loader, the
 * others to their channel client. */
void god_read_stream(uint16_t stream_id);

const MsFileInfo *god_file(uint16_t id);
const MsStreamInfo *god_stream(uint16_t actor_id);
const MsContextRef *god_ctxref(uint16_t context_id);
uint16_t god_context_for_screen(uint16_t screen_id);
const char *god_token_name(uint32_t value);

MsActor *god_actor(uint16_t id);
MsValue *god_var(uint16_t id);
void god_destroy_context(uint16_t id);
void god_destroy_actor(uint16_t id);

/* Channel clients */
void god_register_channel(uint32_t ident, MsActor *a, int sub);
void god_unregister_channel(uint32_t ident);

/* Statistics for msdump */
typedef struct {
	int actors_by_type[256];
	int chunks, functions, variables, responses;
	long code_bytes;
} MsGodStats;
extern MsGodStats ms_god_stats;

#endif
