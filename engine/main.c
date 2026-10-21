/* Il Re Leone - Libro Animato Interattivo (Disney / Media Station, 1995): SDL2 port.
 *
 *   lionking.exe [--data DIR] [--exe FILE] [--fullscreen] [--scale N] [--screen ID] [--trace]
 *                [--interpret] [--click MS:X:Y]... [--mouse MS:down|up|move:X:Y]... [--key MS:CODE]...
 *                [--shot MS:FILE.bmp]... [--quit-after MS] [--seed N]
 *
 * --data: folder with BOOT.STM and the .CXT files (default: DATA next to the executable, then
 * ..\DATA, ..\..\DATA). --exe: the original LIONKING.EXE, for the cursors (default: next to the
 * data folder). --click/--mouse/--key/--shot/--quit-after drive repeatable tests (times in ms from the
 * start). --interpret: run the script bytecode with the interpreter instead of the recompiled C
 * scripts (src\generated). Alt+Enter: full screen. F12: screenshot. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datafile.h"
#include "god.h"
#include "runtime.h"
#include "script.h"
#include "video.h"

extern int audio_init(void);
extern void audio_shutdown(void);
extern int cursors_load_exe(const char *path);

static int file_exists(const char *p) {
	FILE *f = fopen(p, "rb");
	if (f)
		fclose(f);
	return f != NULL;
}

static int find_data(char *out, size_t cap, const char *base) {
	static const char *rel[] = {"DATA", "../DATA", "../../DATA", "."};
	for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
		char p[1024];
		snprintf(p, sizeof(p), "%s%s/BOOT.STM", base, rel[i]);
		if (file_exists(p)) {
			snprintf(out, cap, "%s%s", base, rel[i]);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	char data[1024] = "", exe[1024] = "";
	int fullscreen = 0, scale = 2;
	uint16_t screen = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--data") && v) {
			snprintf(data, sizeof(data), "%s", v);
			i++;
		} else if (!strcmp(a, "--exe") && v) {
			snprintf(exe, sizeof(exe), "%s", v);
			i++;
		} else if (!strcmp(a, "--fullscreen")) {
			fullscreen = 1;
		} else if (!strcmp(a, "--scale") && v) {
			scale = atoi(v);
			i++;
		} else if (!strcmp(a, "--screen") && v) {
			screen = (uint16_t)atoi(v);
			i++;
		} else if (!strcmp(a, "--trace")) {
			script_trace = 1;
		} else if (!strcmp(a, "--interpret")) {
			script_interpret = 1; /* bytecode interpreter instead of the recompiled scripts */
		} else if (!strcmp(a, "--click") && v) {
			unsigned t, x, y;
			if (sscanf(v, "%u:%u:%u", &t, &x, &y) == 3)
				rt_add_scripted_click(t, (int)x, (int)y);
			i++;
		} else if (!strcmp(a, "--mouse") && v) {
			unsigned t, x, y;
			char what[8];
			if (sscanf(v, "%u:%7[a-z]:%u:%u", &t, what, &x, &y) == 4)
				rt_add_scripted_mouse(t, !strcmp(what, "down") ? 0 : !strcmp(what, "up") ? 1 : 2, (int)x, (int)y);
			i++;
		} else if (!strcmp(a, "--key") && v) {
			unsigned t, k;
			if (sscanf(v, "%u:%u", &t, &k) == 2)
				rt_add_scripted_key(t, (int)k);
			i++;
		} else if (!strcmp(a, "--shot") && v) {
			/* the path may contain spaces ("GAME SHOTS"): take everything after the colon */
			unsigned t;
			const char *colon = strchr(v, ':');
			if (sscanf(v, "%u", &t) == 1 && colon && colon[1])
				rt_add_scripted_shot(t, colon + 1);
			i++;
		} else if (!strcmp(a, "--quit-after") && v) {
			rt_set_quit_after((uint32_t)atoi(v));
			i++;
		} else if (!strcmp(a, "--seed") && v) {
			rt_set_seed((uint32_t)strtoul(v, NULL, 0));
			i++;
		} else {
			fprintf(stderr, "unknown option %s (see the comment at the top of main.c)\n", a);
			return 2;
		}
	}

	if (!data[0]) {
		char *base = SDL_GetBasePath();
		int ok = find_data(data, sizeof(data), base ? base : "");
		SDL_free(base);
		if (!ok && !find_data(data, sizeof(data), "")) {
			fprintf(stderr, "Game data not found: use --data FOLDER (the folder with BOOT.STM).\n");
			return 1;
		}
	}
	if (!exe[0])
		snprintf(exe, sizeof(exe), "%s/../LIONKING.EXE", data);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	if (!video_init(fullscreen, scale)) {
		fprintf(stderr, "video: %s\n", SDL_GetError());
		return 1;
	}
	if (!audio_init())
		ms_warn("no audio device: %s", SDL_GetError());
	if (!cursors_load_exe(exe))
		ms_warn("%s not found: the original cursors will not be shown", exe);
	else if (!video_load_dissolve_tables(exe))
		ms_warn("%s: dissolve patterns not found, using an approximation", exe);

	fprintf(stderr, "data: %s\n", data);
	rt_init();
	rt_run(data, screen);

	audio_shutdown();
	video_shutdown();
	SDL_Quit();
	return 0;
}
