#include "god.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"

MsGod ms_god;
MsGodHooks ms_god_hooks;
MsGodStats ms_god_stats;

#define GROW(arr, n) (arr = realloc(arr, sizeof(*(arr)) * (size_t)((n) + 1)), &(arr)[(n)++])

/* ---- channel clients: small open-addressing table ---- */
typedef struct {
	uint32_t ident;
	MsActor *actor;
	int sub;
} Channel;
#define NCHAN 4096
static Channel channels[NCHAN];

static int chan_slot(uint32_t ident) {
	uint32_t h = (ident * 2654435761u) & (NCHAN - 1);
	while (channels[h].ident && channels[h].ident != ident)
		h = (h + 1) & (NCHAN - 1);
	return (int)h;
}

void god_register_channel(uint32_t ident, MsActor *a, int sub) {
	int s = chan_slot(ident);
	if (channels[s].ident && channels[s].actor != a)
		ms_warn("channel %s already has a client", ms_tag_str(ident));
	channels[s].ident = ident;
	channels[s].actor = a;
	channels[s].sub = sub;
}

void god_unregister_channel(uint32_t ident) {
	int s = chan_slot(ident);
	if (!channels[s].ident)
		return;
	channels[s].ident = 0;
	/* re-insert the following entries of the cluster */
	int i = (s + 1) & (NCHAN - 1);
	while (channels[i].ident) {
		Channel c = channels[i];
		channels[i].ident = 0;
		channels[chan_slot(c.ident)] = c;
		i = (i + 1) & (NCHAN - 1);
	}
}

static Channel *chan_find(uint32_t ident) {
	int s = chan_slot(ident);
	return channels[s].ident ? &channels[s] : NULL;
}

/* ---- lookups ---- */

const MsFileInfo *god_file(uint16_t id) {
	for (int i = 0; i < ms_god.nfiles; i++)
		if (ms_god.files[i].id == id)
			return &ms_god.files[i];
	return NULL;
}

const MsStreamInfo *god_stream(uint16_t actor_id) {
	for (int i = 0; i < ms_god.nstreams; i++)
		if (ms_god.streams[i].actor_id == actor_id)
			return &ms_god.streams[i];
	return NULL;
}

const MsContextRef *god_ctxref(uint16_t context_id) {
	for (int i = 0; i < ms_god.nctxrefs; i++)
		if (ms_god.ctxrefs[i].context_id == context_id)
			return &ms_god.ctxrefs[i];
	return NULL;
}

uint16_t god_context_for_screen(uint16_t screen_id) {
	for (int i = 0; i < ms_god.nscreenrefs; i++)
		if (ms_god.screenrefs[i].screen_id == screen_id)
			return ms_god.screenrefs[i].context_id;
	return 0;
}

const char *god_token_name(uint32_t value) {
	for (int i = 0; i < ms_god.ntokens; i++)
		if (ms_god.tokens[i].value == value)
			return ms_god.tokens[i].name;
	return NULL;
}

MsActor *god_actor(uint16_t id) {
	return ms_god.actors[id];
}

MsValue *god_var(uint16_t id) {
	return ms_god.vars[id];
}

/* ---- files ---- */

/* Opens a data file of the title, matching the name case-insensitively. Only one file is
 * kept in memory at a time (like the CD stream of the original). */
static MsFile cur_file;
static uint16_t cur_file_id;

static int open_ci(const char *dir, const char *name, MsFile *f) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (ms_file_load(f, path))
		return 1;
	char up[256], low[256];
	size_t n = strlen(name);
	if (n >= sizeof(up))
		return 0;
	for (size_t i = 0; i <= n; i++) {
		up[i] = (char)toupper((unsigned char)name[i]);
		low[i] = (char)tolower((unsigned char)name[i]);
	}
	snprintf(path, sizeof(path), "%s/%s", dir, up);
	if (ms_file_load(f, path))
		return 1;
	snprintf(path, sizeof(path), "%s/%s", dir, low);
	return ms_file_load(f, path);
}

static MsFile *file_for(uint16_t file_id) {
	if (cur_file.data && cur_file_id == file_id)
		return &cur_file;
	const MsFileInfo *fi = god_file(file_id);
	if (!fi)
		ms_fatal("file %u not declared in BOOT.STM", file_id);
	ms_file_free(&cur_file);
	if (!open_ci(ms_god.data_dir, fi->name, &cur_file))
		ms_fatal("cannot open %s/%s", ms_god.data_dir, fi->name);
	cur_file_id = file_id;
	return &cur_file;
}

/* ---- igod chunks ---- */

static void read_document_def(MsChunk *c);
static void read_control_commands(MsChunk *c);

static void read_igod(MsChunk *c) {
	uint16_t type = ms_t_u16(c);
	if (type == STREAM_DOCUMENT_DEF)
		read_document_def(c);
	else if (type == STREAM_CONTROL_COMMANDS)
		read_control_commands(c);
	else
		ms_fatal("igod: unknown stream type 0x%x", type);
}

static void read_context_refs(MsChunk *c) {
	while (ms_t_u16(c) != 0) {
		MsContextRef *r = GROW(ms_god.ctxrefs, ms_god.nctxrefs);
		memset(r, 0, sizeof(*r));
		if (ms_t_u16(c) != CTXREF_CONTEXT_ID)
			ms_fatal("context reference: expected context id");
		r->context_id = ms_t_u16(c);
		if (ms_t_u16(c) != CTXREF_STREAM_ID)
			ms_fatal("context reference: expected stream id");
		r->stream_id = ms_t_u16(c);
		uint16_t s = ms_t_u16(c);
		if (s == CTXREF_NAME) {
			r->name = ms_t_string(c);
			s = ms_t_u16(c);
		}
		/* as in the original: without parents the section just read is consumed */
		size_t rewind = c->pos;
		while (s == CTXREF_PARENT_CONTEXT_ID) {
			uint16_t p = ms_t_u16(c);
			if (r->nparents < 16)
				r->parents[r->nparents++] = p;
			rewind = c->pos;
			s = ms_t_u16(c);
		}
		/* after the parents, the section that ended the list is the "flag" of the next one */
		c->pos = rewind;
	}
}

static void read_document_def(MsChunk *c) {
	for (;;) {
		uint16_t s = ms_t_u16(c);
		if (s == BOOT_LAST)
			break;
		switch (s) {
		case BOOT_VERSION_INFO:
			ms_god.title = ms_t_string(c);
			ms_t_version(c, &ms_god.ver_major, &ms_god.ver_minor, &ms_god.ver_patch);
			ms_god.engine_info = ms_t_string(c);
			ms_god.source = ms_t_string(c);
			break;
		case BOOT_CONTEXT_REFERENCE:
			read_context_refs(c);
			break;
		case BOOT_SCREEN_REFERENCE:
			while (ms_t_u16(c) != 0) {
				MsScreenRef *r = GROW(ms_god.screenrefs, ms_god.nscreenrefs);
				if (ms_t_u16(c) != SCREENREF_SCREEN_ID)
					ms_fatal("screen reference: expected screen id");
				r->screen_id = ms_t_u16(c);
				if (ms_t_u16(c) != SCREENREF_CONTEXT_ID)
					ms_fatal("screen reference: expected context id");
				r->context_id = ms_t_u16(c);
			}
			break;
		case BOOT_FILE_INFO:
			while (ms_t_u16(c) != 0) {
				MsFileInfo *f = GROW(ms_god.files, ms_god.nfiles);
				if (ms_t_u16(c) != FILEINFO_FILE_ID)
					ms_fatal("file info: expected file id");
				f->id = ms_t_u16(c);
				if (ms_t_u16(c) != FILEINFO_NAME_AND_TYPE)
					ms_fatal("file info: expected name");
				f->location = ms_t_u16(c);
				f->name = ms_t_filename(c);
			}
			break;
		case BOOT_STREAM_INFO:
			while (ms_t_u16(c) != 0) {
				MsStreamInfo *si = GROW(ms_god.streams, ms_god.nstreams);
				if (ms_t_u16(c) != STREAMINFO_ACTOR_ID)
					ms_fatal("stream info: expected actor id");
				si->actor_id = ms_t_u16(c);
				if (ms_t_u16(c) != STREAMINFO_FILE_ID)
					ms_fatal("stream info: expected file id");
				si->file_id = ms_t_u16(c);
				if (ms_t_u16(c) != STREAMINFO_START_OFFSET)
					ms_fatal("stream info: expected start offset");
				si->offset = ms_t_u32(c);
			}
			break;
		case BOOT_UNK1:
		case BOOT_UNK3:
			ms_t_u16(c);
			break;
		case BOOT_FUNCTION_TABLE_SIZE:
			ms_god.function_table_size = ms_t_u16(c);
			break;
		case BOOT_PARAM_TOKEN_DECL: {
			MsParamToken *t = GROW(ms_god.tokens, ms_god.ntokens);
			t->name = ms_t_string(c);
			if (ms_t_u16(c) != BOOT_PARAM_TOKEN_VALUE)
				ms_warn("param token %s: bad separator", t->name);
			char *v = ms_t_string(c);
			t->value = (uint32_t)atoi(v);
			free(v);
			break;
		}
		default:
			/* the other sections are commands of the "parameter clients" */
			{
				void read_command(MsChunk * c, uint16_t s);
				read_command(c, s);
			}
		}
	}
}

/* ---- contexts, actors, variables, functions ---- */

static MsContext *context_get(uint16_t id) {
	return ms_god.contexts[id];
}

static void context_add_var(MsContext *ctx, uint16_t id) {
	if (ctx->nvars == ctx->capvars) {
		ctx->capvars = ctx->capvars ? ctx->capvars * 2 : 32;
		ctx->vars = realloc(ctx->vars, sizeof(uint16_t) * (size_t)ctx->capvars);
	}
	ctx->vars[ctx->nvars++] = id;
}

void god_destroy_actor(uint16_t id) {
	MsActor *a = ms_god.actors[id];
	if (!a) {
		ms_warn("destroy actor %u: not loaded", id);
		return;
	}
	ms_god.actors[id] = NULL;
	actor_destroy(a);
}

void god_destroy_context(uint16_t id) {
	MsContext *ctx = ms_god.contexts[id];
	if (!ctx) {
		ms_warn("destroy context %u: not loaded", id);
		return;
	}
	for (int i = 0; i < MS_MAX_ID; i++)
		if (ms_god.actors[i] && ms_god.actors[i]->context == id)
			god_destroy_actor((uint16_t)i);
	for (int i = 0; i < MS_MAX_ID; i++)
		if (ms_god.functions[i] && ms_god.functions[i]->context == id) {
			free(ms_god.functions[i]->code);
			free(ms_god.functions[i]);
			ms_god.functions[i] = NULL;
		}
	for (int i = 0; i < ctx->nvars; i++) {
		MsValue *v = ms_god.vars[ctx->vars[i]];
		if (v && ms_god.var_context[ctx->vars[i]] == id) {
			val_free(v);
			free(v);
			ms_god.vars[ctx->vars[i]] = NULL;
		}
	}
	free(ctx->vars);
	free(ctx->name);
	free(ctx);
	ms_god.contexts[id] = NULL;
}

static void read_function(MsChunk *c) {
	MsFunction *f = calloc(1, sizeof(MsFunction));
	f->context = ms_t_u16(c);
	f->id = ms_t_u16(c);
	f->size = ms_t_u32(c);
	f->code = malloc(f->size ? f->size : 1);
	ms_read(c, f->code, f->size);
	/* LIONKING.EXE 1198:824c appends every function to a list and the lookup (1198:82dc) finds
	 * the first one with the id: a redefinition is never used */
	if (ms_god.functions[f->id]) {
		ms_warn("function %u defined again: the first definition stays", f->id);
		free(f->code);
		free(f);
		return;
	}
	ms_god.functions[f->id] = f;
	ms_god_stats.functions++;
	ms_god_stats.code_bytes += f->size;
}

/* Commands of the "parameter clients". LIONKING.EXE: the actor map (1190:3c88) reads 0x0e,
 * 0x0f, 0x11, 0x12, 0x13, 0x14; the document 0x10 and 0x2e (1190:2f08); the function manager
 * 0x31 (1198:824c); the display 0x578..0x57b and 0x5aa (1190:a482); the cursor manager 0x0c,
 * 0x15, 0x16 (1190:659a). A command no client reads is skipped without error (1190:48e8
 * just asks every client); its data is then read as the next commands. */
void read_command(MsChunk *c, uint16_t s) {
	switch (s) {
	case CMD_CREATE_CONTEXT: {
		/* 1190:3d14: only a context with a reference in BOOT.STM (1190:42be) is created */
		uint16_t id = ms_t_u16(c);
		if (!god_ctxref(id)) {
			ms_warn("create context %u: no context reference, ignored", id);
			break;
		}
		if (!ms_god.contexts[id]) {
			MsContext *ctx = calloc(1, sizeof(MsContext));
			ctx->id = id;
			ms_god.contexts[id] = ctx;
		}
		break;
	}
	case CMD_DESTROY_CONTEXT:
		god_destroy_context(ms_t_u16(c));
		break;
	case CMD_CONTEXT_NAME: {
		uint16_t id = ms_t_u16(c);
		MsContext *ctx = context_get(id);
		char *name = ms_t_string(c);
		if (!ctx) {
			ms_warn("name of context %u that is not loaded", id);
			free(name);
		} else {
			free(ctx->name);
			ctx->name = name;
		}
		break;
	}
	case CMD_CREATE_ACTOR: {
		uint16_t context = ms_t_u16(c);
		uint16_t type = ms_t_u16(c);
		uint16_t id = ms_t_u16(c);
		if (ms_god.actors[id])
			ms_fatal("actor %u already defined", id);
		MsActor *a = actor_create(context, type, id, c);
		ms_god.actors[id] = a;
		ms_god_stats.actors_by_type[type & 0xff]++;
		break;
	}
	case CMD_DESTROY_ACTOR:
		god_destroy_actor(ms_t_u16(c));
		break;
	case CMD_ACTOR_LOAD_COMPLETE: {
		uint16_t id = ms_t_u16(c);
		MsActor *a = ms_god.actors[id];
		/* 1190:3fde: an unknown actor is skipped; a second load complete runs again */
		if (!a) {
			ms_warn("load complete of unknown actor %u", id);
			break;
		}
		a->loaded = 1;
		if (a->vt->load_complete)
			a->vt->load_complete(a);
		if (ms_god_hooks.actor_loaded)
			ms_god_hooks.actor_loaded(a);
		break;
	}
	case CMD_CREATE_VARIABLE: {
		uint16_t context = ms_t_u16(c);
		uint16_t id = ms_t_u16(c);
		MsValue *v = malloc(sizeof(MsValue));
		*v = val_read(c);
		/* 1190:3e40: the value is read, then the variable is made only if its context exists */
		MsContext *ctx = context_get(context);
		if (!ctx || ms_god.vars[id]) {
			ms_warn("variable %u of context %u: %s, ignored", id, context,
				ctx ? "already defined" : "context not loaded");
			val_free(v);
			free(v);
			break;
		}
		ms_god.vars[id] = v;
		ms_god.var_context[id] = context;
		context_add_var(ctx, id);
		ms_god_stats.variables++;
		break;
	}
	case CMD_FUNCTION:
		read_function(c);
		break;
	case CMD_CONTEXT_LOAD_COMPLETE: {
		uint16_t id = ms_t_u16(c);
		MsContext *ctx = context_get(id);
		if (ctx)
			ctx->loaded = 1;
		if (ms_god_hooks.context_load_complete)
			ms_god_hooks.context_load_complete(id);
		break;
	}
	case CMD_STARTUP_INFO: {
		uint16_t t = ms_t_u16(c);
		if (t != CMD_ENTRY_SCREEN)
			ms_fatal("startup information: unknown section 0x%x", t);
		uint16_t screen = ms_t_u16(c);
		if (!ms_god.entry_screen)
			ms_god.entry_screen = screen;
		break;
	}
	case CMD_ALLOW_MULTIPLE_SOUNDS:
	case CMD_ALLOW_MULTIPLE_STREAMS:
		/* later engines; no client of LIONKING.EXE reads them (skipped like any unknown
		 * command, the byte that follows is read as the next command) */
		ms_warn("command 0x%x not read by this engine", s);
		break;
	case CMD_CURSOR_INIT:
		ms_god.cursor_base = ms_t_u16(c);
		ms_god.cursor_max = ms_t_u16(c);
		break;
	case CMD_NEW_CURSOR: {
		uint16_t type = ms_t_u16(c);
		uint16_t id = ms_t_u16(c);
		if (type == 0) {
			uint16_t platform = ms_t_u16(c);
			if (ms_god_hooks.new_cursor)
				ms_god_hooks.new_cursor(id, 0, platform, NULL);
		} else if (type == 1) {
			ms_t_u16(c);
			char *res = ms_t_filename(c);
			if (ms_god_hooks.new_cursor)
				ms_god_hooks.new_cursor(id, 1, 0, res);
			free(res);
		} else {
			ms_fatal("new cursor: unknown type %u", type);
		}
		break;
	}
	case CMD_DISPOSE_CURSOR: {
		uint16_t id = ms_t_u16(c);
		if (ms_god_hooks.dispose_cursor)
			ms_god_hooks.dispose_cursor(id);
		break;
	}
	case CMD_DISPLAY_UPDATE_DIRTY:
		if (ms_god_hooks.display_update_dirty)
			ms_god_hooks.display_update_dirty();
		break;
	case CMD_DISPLAY_UPDATE_ALL:
		if (ms_god_hooks.display_update_all)
			ms_god_hooks.display_update_all();
		break;
	case CMD_DISPLAY_EFFECT_TRANSITION: {
		uint16_t n = ms_t_u16(c);
		MsValue args[16];
		int k = 0;
		for (int i = 0; i < n; i++) {
			MsValue v = val_read(c);
			if (k < 16)
				args[k++] = v;
			else
				val_free(&v);
		}
		if (ms_god_hooks.display_transition)
			ms_god_hooks.display_transition(args, k);
		for (int i = 0; i < k; i++)
			val_free(&args[i]);
		break;
	}
	case CMD_DISPLAY_SET_TIME:
		ms_god.default_transition_time = ms_t_time(c);
		break;
	case CMD_DISPLAY_LOAD_PALETTE:
		ms_read(c, ms_god.palette, 768);
		ms_god.have_palette = 1;
		break;
	default:
		ms_warn("unknown command 0x%x (@0x%zx): no client reads it", s, c->pos);
	}
}

static void read_control_commands(MsChunk *c) {
	for (;;) {
		uint16_t s = ms_t_u16(c);
		if (s == CMD_END)
			break;
		read_command(c, s);
	}
}

/* ---- streams ---- */

static void dispatch_chunk(MsChunk *c) {
	ms_god_stats.chunks++;
	if (c->id == MS_TAG('i', 'g', 'o', 'd')) {
		if (ms_remaining(c) > 0)
			read_igod(c);
	} else {
		Channel *ch = chan_find(c->id);
		if (!ch)
			ms_fatal("chunk %s @0x%zx: no client for this channel", ms_tag_str(c->id), c->start - 8);
		if (ms_remaining(c) > 0 && ch->actor->vt->read_chunk)
			ch->actor->vt->read_chunk(ch->actor, ch->sub, c);
	}
	if (ms_remaining(c) != 0)
		ms_warn("chunk %s @0x%zx: %zu bytes not read", ms_tag_str(c->id), c->start - 8,
			ms_remaining(c));
}

static void read_subfile(MsFile *f, size_t offset) {
	MsSubfile sf;
	ms_subfile_open(&sf, f, offset);
	size_t pos = sf.first_chunk;
	MsChunk c;
	while (ms_subfile_next_chunk(&sf, &pos, &c))
		dispatch_chunk(&c);
}

void god_read_stream(uint16_t stream_id) {
	const MsStreamInfo *si = god_stream(stream_id);
	if (!si)
		ms_fatal("stream %u not declared", stream_id);
	MsFile *f = file_for(si->file_id);
	read_subfile(f, si->offset);
}

void god_boot(const char *data_dir) {
	snprintf(ms_god.data_dir, sizeof(ms_god.data_dir), "%s", data_dir);
	/* BOOT.STM is file 1 / stream 1, implicitly */
	MsFileInfo *fi = GROW(ms_god.files, ms_god.nfiles);
	fi->id = 1;
	fi->location = 0;
	fi->name = strcpy(malloc(9), "BOOT.STM");
	MsStreamInfo *si = GROW(ms_god.streams, ms_god.nstreams);
	si->actor_id = 1;
	si->file_id = 1;
	si->offset = 0;
	god_read_stream(1);
}
