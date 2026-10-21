/* msdump: inspects Media Station data files.
 *   msdump chunks FILE...      subfiles and chunks of each file
 *   msdump boot DATA_DIR       what BOOT.STM declares
 *   msdump load DATA_DIR [ID]  loads every context (or one) and lists its actors
 *   msdump scripts DATA_DIR [ID]  decompiled scripts of every context (or one)
 *   msdump streams DATA_DIR [ID]  same, also reading the streams of movies and sounds
 *   msdump transpile DATA_DIR [OUTDIR]  every script translated to C (default OUTDIR: generated)
 *   msdump vars DATA_DIR [ID]     variables of every context (or one) with their initial values
 * Options before the command: --names FILE (names table, default names.txt next to msdump.exe,
 * then ..\tools\msdump\names.txt from it, then tools\msdump\names.txt) or --no-names (raw ids).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "datafile.h"
#include "god.h"
#include "script.h"
#include "transpile.h"

static void cmd_chunks(const char *path) {
	MsFile f;
	if (!ms_file_load(&f, path))
		ms_fatal("cannot read %s", path);
	size_t pos = 0;
	if (f.size >= 16 && f.data[0] == 'I' && f.data[1] == 'I') {
		MsChunk h = {f.data, 0, 16, 0, 0};
		ms_skip(&h, 4);
		uint32_t unk = ms_u32(&h), count = ms_u32(&h), size = ms_u32(&h);
		printf("%s: header II unk=0x%x subfiles=%u size=%u (real %zu)\n", path, unk, count, size, f.size);
		pos = 16;
	} else {
		printf("%s: no header, size %zu\n", path, f.size);
	}
	int n = 0;
	while (pos + 12 <= f.size && !memcmp(f.data + pos, "RIFF", 4)) {
		MsSubfile sf;
		size_t next = ms_subfile_open(&sf, &f, pos);
		size_t cp = sf.first_chunk;
		MsChunk c;
		printf("  subfile %d @0x%zx rate=%u:", n, pos, sf.rate);
		int k = 0;
		uint32_t last = 0;
		int rep = 0;
		while (ms_subfile_next_chunk(&sf, &cp, &c)) {
			if (c.id == last) {
				rep++;
				continue;
			}
			if (rep)
				printf(" x%d", rep + 1);
			rep = 0;
			last = c.id;
			printf(" %s(%zu)", ms_tag_str(c.id), c.end - c.start);
			if (++k > 40) {
				printf(" ...");
				break;
			}
		}
		if (rep)
			printf(" x%d", rep + 1);
		printf("\n");
		pos = next;
		if (pos & 1)
			pos++;
		n++;
	}
	if (pos < f.size)
		printf("  %zu bytes after the last subfile\n", f.size - pos);
	ms_file_free(&f);
}

static void cmd_boot(const char *dir) {
	god_boot(dir);
	MsGod *g = &ms_god;
	printf("title \"%s\" version %u.%u.%u\nengine \"%s\"\nsource \"%s\"\n", g->title ? g->title : "",
		g->ver_major, g->ver_minor, g->ver_patch, g->engine_info ? g->engine_info : "",
		g->source ? g->source : "");
	printf("entry screen %u, function table size %u, cursors %u..%u\n", g->entry_screen,
		g->function_table_size, g->cursor_base, g->cursor_max);
	printf("\n%d files:\n", g->nfiles);
	for (int i = 0; i < g->nfiles; i++)
		printf("  %4u loc %u %s\n", g->files[i].id, g->files[i].location, g->files[i].name);
	printf("\n%d streams:\n", g->nstreams);
	for (int i = 0; i < g->nstreams; i++)
		printf("  actor %5u  file %4u  @0x%x\n", g->streams[i].actor_id, g->streams[i].file_id,
			g->streams[i].offset);
	printf("\n%d contexts:\n", g->nctxrefs);
	for (int i = 0; i < g->nctxrefs; i++) {
		MsContextRef *r = &g->ctxrefs[i];
		printf("  context %5u stream %5u %s parents:", r->context_id, r->stream_id, r->name ? r->name : "");
		for (int k = 0; k < r->nparents; k++)
			printf(" %u", r->parents[k]);
		printf("\n");
	}
	printf("\n%d screens:\n", g->nscreenrefs);
	for (int i = 0; i < g->nscreenrefs; i++)
		printf("  screen %5u -> context %u\n", g->screenrefs[i].screen_id, g->screenrefs[i].context_id);
	printf("\n%d param tokens:\n", g->ntokens);
	for (int i = 0; i < g->ntokens; i++)
		printf("  %5u %s\n", g->tokens[i].value, g->tokens[i].name);
	printf("\nfunctions after boot: %d, variables: %d, actors:", ms_god_stats.functions,
		ms_god_stats.variables);
	for (int t = 0; t < 256; t++)
		if (ms_god_stats.actors_by_type[t])
			printf(" %s=%d", actor_type_name(t), ms_god_stats.actors_by_type[t]);
	printf("\n");
}

static int play_streams, streams_read;

static void print_context(uint16_t id);

static void load_context(uint16_t id, int list) {
	if (ms_god.contexts[id] && ms_god.contexts[id]->loaded) {
		/* a root context (the common context 1100) is loaded by god_boot: list it too */
		if (list)
			print_context(id);
		return;
	}
	const MsContextRef *r = god_ctxref(id);
	if (!r)
		ms_fatal("context %u not declared", id);
	for (int k = 0; k < r->nparents; k++)
		if (r->parents[k] && r->parents[k] != id)
			load_context(r->parents[k], 0);
	int before[MS_MAX_ID / 8] = {0};
	(void)before;
	god_read_stream(r->stream_id);
	if (!list)
		return;
	if (play_streams)
		for (int i = 0; i < MS_MAX_ID; i++) {
			MsActor *a = ms_god.actors[i];
			if (a && a->context == id && god_stream(a->id) && (a->type == ACTOR_MOVIE || a->type == ACTOR_SOUND)) {
				MsValue r = actor_call_method(a, M_TIME_PLAY, NULL, 0);
				val_free(&r);
				streams_read++;
			}
		}
	print_context(id);
}

static void print_context(uint16_t id) {
	MsContext *ctx = ms_god.contexts[id];
	printf("context %u %s:\n", id, ctx && ctx->name ? ctx->name : "");
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsActor *a = ms_god.actors[i];
		if (!a || a->context != id)
			continue;
		const char *nm = names_get('a', a->id);
		printf("  %5u %-12s %-24s", a->id, a->vt->name, nm ? nm : a->name ? a->name : "");
		if (a->vt->spatial)
			printf(" box %d,%d %dx%d z%d%s", a->box.x, a->box.y, a->box.w, a->box.h, a->z,
				a->visible ? " visible" : "");
		if (a->vt->spatial)
			printf(" stage %u", a->stage_id);
		printf(" resp %d%s\n", a->nresp, a->loaded ? "" : " NOT-LOADED");
	}
	int nv = 0;
	for (int i = 0; i < MS_MAX_ID; i++)
		if (ms_god.vars[i] && ms_god.var_context[i] == id)
			nv++;
	printf("  variables: %d\n", nv);
}

static const char *event_name(int ev) {
	switch (ev) {
	case EV_TIMER:
		return "Time";
	case EV_MOUSE_DOWN:
		return "MouseDown";
	case EV_MOUSE_UP:
		return "MouseUp";
	case EV_MOUSE_MOVED:
		return "MouseMoved";
	case EV_MOUSE_ENTERED:
		return "MouseEntered";
	case EV_MOUSE_EXITED:
		return "MouseExited";
	case EV_KEY_DOWN:
		return "KeyDown";
	case EV_SOUND_END:
		return "SoundEnd";
	case EV_MOVIE_END:
		return "MovieEnd";
	case EV_PATH_END:
		return "PathEnd";
	case EV_SCREEN_ENTRY:
		return "ScreenEntry";
	case EV_SOUND_ABORT:
		return "SoundAbort";
	case EV_SOUND_FAILURE:
		return "SoundFailure";
	case EV_MOVIE_ABORT:
		return "MovieAbort";
	case EV_MOVIE_FAILURE:
		return "MovieFailure";
	case EV_SPRITE_MOVIE_END:
		return "SpriteMovieEnd";
	case EV_MINIGAME_SUCCESS:
		return "MinigameSuccess";
	case EV_MINIGAME_DEFEAT:
		return "MinigameDefeat";
	case EV_MINIGAME_CROUCHED:
		return "MinigameCrouched";
	case EV_SCREEN_EXIT:
		return "ScreenExit";
	case EV_PATH_STEP:
		return "PathStep";
	case EV_SOUND_STOPPED:
		return "SoundStopped";
	case EV_SOUND_BEGIN:
		return "SoundBegin";
	case EV_MOVIE_STOPPED:
		return "MovieStopped";
	case EV_MOVIE_BEGIN:
		return "MovieBegin";
	case EV_PATH_STOPPED:
		return "PathStopped";
	case EV_CONTEXT_LOAD_COMPLETE:
		return "ContextLoadComplete";
	case EV_CONTEXT_ALREADY_LOADED:
		return "ContextAlreadyLoaded";
	case EV_CONTEXT_RELEASE_COMPLETE:
		return "ContextReleaseComplete";
	case EV_CONTEXT_ALREADY_RELEASED:
		return "ContextAlreadyReleased";
	}
	return "?";
}

/* Decompiled scripts of the actors of a context (and its functions). */
static void print_scripts(uint16_t context) {
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsActor *a = ms_god.actors[i];
		if (!a || a->context != context || a->nresp == 0)
			continue;
		const char *nm = names_get('a', a->id);
		printf("\n== actor %u %s %s", a->id, a->vt->name, nm ? nm : a->name ? a->name : "");
		if (a->vt->spatial)
			printf(" box %d,%d %dx%d z%d", a->box.x, a->box.y, a->box.w, a->box.h, a->z);
		printf("\n");
		for (int k = 0; k < a->nresp; k++) {
			MsResponse *r = &a->resp[k];
			printf("On %s(0x%x)", event_name(r->event), r->event);
			if (r->arg.type)
				printf(" %s", val_debug(&r->arg));
			printf("\n");
			char *s = script_decompile(r->code, r->size, 1);
			char *named = names_apply(s);
			fputs(named, stdout);
			free(named);
			free(s);
			printf("End\n");
		}
	}
	for (int i = 0; i < MS_MAX_ID; i++) {
		MsFunction *f = ms_god.functions[i];
		if (!f || f->context != context)
			continue;
		const char *nm = names_get('f', f->id);
		printf("\n== function_%u%s%s (context %u)\n", f->id, nm ? " " : "", nm ? nm : "", f->context);
		char *s = script_decompile(f->code, f->size, 1);
		char *named = names_apply(s);
		fputs(named, stdout);
		free(named);
		free(s);
		printf("End\n");
	}
}

static void cmd_scripts(const char *dir, int only) {
	god_boot(dir);
	if (only) {
		load_context((uint16_t)only, 0);
		print_scripts((uint16_t)only);
		return;
	}
	/* contexts declared by BOOT.STM itself (variables, functions and actors) */
	for (int c = 0; c < MS_MAX_ID; c++)
		if (ms_god.contexts[c]) {
			printf("\n######## context %d%s%s (BOOT.STM)\n", c, names_get('c', (unsigned)c) ? " " : "",
				names_get('c', (unsigned)c) ? names_get('c', (unsigned)c) : "");
			print_scripts((uint16_t)c);
		}
	for (int i = 0; i < ms_god.nctxrefs; i++) {
		uint16_t id = ms_god.ctxrefs[i].context_id;
		if (ms_god.contexts[id])
			continue;
		load_context(id, 0);
		printf("\n######## context %u%s%s\n", id, names_get('c', id) ? " " : "",
			names_get('c', id) ? names_get('c', id) : "");
		print_scripts(id);
		god_destroy_context(id);
	}
}

/* Every script of the game translated to C (src\generated), context by context like cmd_scripts. */
static int cmd_transpile(const char *dir, const char *outdir) {
	god_boot(dir);
	transpile_begin(outdir);
	for (int c = 0; c < MS_MAX_ID; c++)
		if (ms_god.contexts[c])
			transpile_context((uint16_t)c);
	for (int i = 0; i < ms_god.nctxrefs; i++) {
		uint16_t id = ms_god.ctxrefs[i].context_id;
		if (ms_god.contexts[id])
			continue;
		load_context(id, 0);
		transpile_context(id);
		god_destroy_context(id);
	}
	return transpile_end() ? 1 : 0;
}

/* Variables created by a context, with their initial values and names. */
static void print_vars(uint16_t context) {
	MsContext *ctx = ms_god.contexts[context];
	if (!ctx)
		return;
	const char *cn = names_get('c', context);
	printf("context %u%s%s: %d variables\n", context, cn ? " " : "", cn ? cn : "", ctx->nvars);
	for (int i = 0; i < ctx->nvars; i++) {
		uint16_t v = ctx->vars[i];
		const char *nm = names_get('v', v);
		printf("  var_%-5u %-28s = %s\n", v, nm ? nm : "", ms_god.vars[v] ? val_debug(ms_god.vars[v]) : "?");
	}
}

static void cmd_vars(const char *dir, int only) {
	god_boot(dir);
	for (int c = 0; c < MS_MAX_ID; c++)
		if (ms_god.contexts[c] && (!only || c == only))
			print_vars((uint16_t)c);
	for (int i = 0; i < ms_god.nctxrefs; i++) {
		uint16_t id = ms_god.ctxrefs[i].context_id;
		if (ms_god.contexts[id] || (only && id != only))
			continue;
		load_context(id, 0);
		print_vars(id);
		god_destroy_context(id);
	}
}

static void cmd_load(const char *dir, int only) {
	god_boot(dir);
	if (only) {
		load_context((uint16_t)only, 1);
	} else {
		for (int i = 0; i < ms_god.nctxrefs; i++) {
			uint16_t id = ms_god.ctxrefs[i].context_id;
			load_context(id, 1);
			/* unload everything except the root contexts, like a screen change would */
			for (int k = 0; k < MS_MAX_ID; k++)
				if (ms_god.contexts[k] && k != id) {
					int is_parent = 0;
					const MsContextRef *r = god_ctxref(id);
					for (int p = 0; r && p < r->nparents; p++)
						if (r->parents[p] == k)
							is_parent = 1;
					if (!is_parent)
						god_destroy_context((uint16_t)k);
				}
		}
	}
	if (play_streams)
		printf("\nstreams read %d", streams_read);
	printf("\nchunks %d, functions %d, variables %d, responses %d, bytecode %ld bytes\nactors:",
		ms_god_stats.chunks, ms_god_stats.functions, ms_god_stats.variables, ms_god_stats.responses,
		ms_god_stats.code_bytes);
	for (int t = 0; t < 256; t++)
		if (ms_god_stats.actors_by_type[t])
			printf(" %s=%d", actor_type_name(t), ms_god_stats.actors_by_type[t]);
	printf("\n");
}

/* Loads the names table: --names FILE, else the default locations (see the top of the file). */
static void load_names(const char *exe, const char *file) {
	if (file) {
		if (names_load(file) < 0)
			ms_fatal("cannot read the names table %s", file);
		return;
	}
	char dir[600];
	snprintf(dir, sizeof(dir), "%s", exe);
	char *slash = strrchr(dir, '/');
	char *bslash = strrchr(dir, '\\');
	if (bslash > slash)
		slash = bslash;
	if (slash)
		slash[1] = 0;
	else
		dir[0] = 0;
	char path[700];
	snprintf(path, sizeof(path), "%snames.txt", dir);
	if (names_load(path) >= 0)
		return;
	snprintf(path, sizeof(path), "%s../tools/msdump/names.txt", dir);
	if (names_load(path) >= 0)
		return;
	names_load("tools/msdump/names.txt");
}

int main(int argc, char **argv) {
	const char *names_file = NULL;
	int use_names = 1;
	while (argc > 1 && !strncmp(argv[1], "--", 2)) {
		if (!strcmp(argv[1], "--no-names")) {
			use_names = 0;
		} else if (!strcmp(argv[1], "--names") && argc > 2) {
			names_file = argv[2];
			argv++;
			argc--;
		} else {
			fprintf(stderr, "unknown option %s\n", argv[1]);
			return 2;
		}
		argv++;
		argc--;
	}
	if (use_names)
		load_names(argv[0], names_file);
	if (argc < 3) {
		fprintf(stderr, "usage: msdump [--names FILE|--no-names] chunks FILE... | boot DATA_DIR |\n"
			"  load|scripts|streams|vars DATA_DIR [CONTEXT] | transpile DATA_DIR [OUTDIR]\n");
		return 2;
	}
	if (!strcmp(argv[1], "chunks")) {
		for (int i = 2; i < argc; i++)
			cmd_chunks(argv[i]);
	} else if (!strcmp(argv[1], "boot")) {
		cmd_boot(argv[2]);
	} else if (!strcmp(argv[1], "scripts")) {
		cmd_scripts(argv[2], argc > 3 ? atoi(argv[3]) : 0);
	} else if (!strcmp(argv[1], "vars")) {
		cmd_vars(argv[2], argc > 3 ? atoi(argv[3]) : 0);
	} else if (!strcmp(argv[1], "transpile")) {
		return cmd_transpile(argv[2], argc > 3 ? argv[3] : "generated");
	} else if (!strcmp(argv[1], "streams")) {
		play_streams = 1;
		cmd_load(argv[2], argc > 3 ? atoi(argv[3]) : 0);
	} else if (!strcmp(argv[1], "load")) {
		cmd_load(argv[2], argc > 3 ? atoi(argv[3]) : 0);
	} else {
		fprintf(stderr, "unknown command %s\n", argv[1]);
		return 2;
	}
	return 0;
}
