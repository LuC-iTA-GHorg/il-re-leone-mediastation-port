/* Il Re Leone - Libro Animato Interattivo (Disney / Media Station, 1995): SDL2 port.
 *
 *   lionking.exe [--data DIR] [--exe FILE] [--fullscreen|--windowed] [--scale N]
 *                [--fades palette|instant] [--aspect 4:3|stretch|integer] [--filter sharp|smooth]
 *                [--no-ini] [--screen ID] [--trace] [--interpret] [--click MS:X:Y]...
 *                [--mouse MS:down|up|move:X:Y]... [--key MS:CODE]... [--shot MS:FILE.bmp]...
 *                [--quit-after MS] [--seed N]
 *
 * Display options are read first from lionking.ini next to the executable (written by
 * launcher.exe: lines "key=value" with the keys fullscreen, scale, fades, aspect, filter and the
 * values of the options above), then from the command line, which wins. --no-ini ignores the
 * file (the automatic tests use it). --fades instant: palette fades jump to the final palette,
 * as the exe does on a 16/24-bit screen (what OTVDM shows today); palette (default): timed
 * fades, as the exe on the 256-colour screen the game was made for. --aspect: 4:3 with black
 * bars (default), stretch to fill the screen, or integer scale factors only.
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

/* Sets one display option from the ini file or the command line (0 = unknown value). Italian
 * values are accepted too, as the launcher shows them. */
static int set_option(VideoOptions *o, const char *key, const char *v) {
	if (!strcmp(key, "fullscreen"))
		o->fullscreen = atoi(v) != 0 || !strcmp(v, "yes") || !strcmp(v, "si") || !strcmp(v, "true");
	else if (!strcmp(key, "scale"))
		o->scale = atoi(v);
	else if (!strcmp(key, "fades") && (!strcmp(v, "palette") || !strcmp(v, "instant") || !strcmp(v, "istantanee")))
		o->instant_fades = strcmp(v, "palette") != 0;
	else if (!strcmp(key, "aspect") && !strcmp(v, "4:3"))
		o->aspect = VIDEO_ASPECT_4_3;
	else if (!strcmp(key, "aspect") && !strcmp(v, "stretch"))
		o->aspect = VIDEO_ASPECT_STRETCH;
	else if (!strcmp(key, "aspect") && (!strcmp(v, "integer") || !strcmp(v, "intera")))
		o->aspect = VIDEO_ASPECT_INTEGER;
	else if (!strcmp(key, "filter") && (!strcmp(v, "sharp") || !strcmp(v, "nitido")))
		o->smooth = 0;
	else if (!strcmp(key, "filter") && (!strcmp(v, "smooth") || !strcmp(v, "morbido")))
		o->smooth = 1;
	else
		return 0;
	return 1;
}

static void read_ini(VideoOptions *o) {
	char *base = SDL_GetBasePath();
	char path[1024], line[256];
	snprintf(path, sizeof(path), "%slionking.ini", base ? base : "");
	SDL_free(base);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (line[0] == ';' || line[0] == '#' || line[0] == '[' || !eq)
			continue;
		*eq = 0;
		char *key = line, *v = eq + 1;
		while (*key == ' ' || *key == '\t')
			key++;
		char *e = eq;
		while (e > key && (e[-1] == ' ' || e[-1] == '\t'))
			*--e = 0;
		while (*v == ' ' || *v == '\t')
			v++;
		v[strcspn(v, " \t\r\n;#")] = 0;
		if (!set_option(o, key, v))
			ms_warn("%s: unknown setting %s=%s", path, key, v);
	}
	fclose(f);
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
	VideoOptions vo = {0, 2, VIDEO_ASPECT_4_3, 0, 0};
	uint16_t screen = 0;
	int use_ini = 1;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--no-ini"))
			use_ini = 0;
	if (use_ini)
		read_ini(&vo);
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
			vo.fullscreen = 1;
		} else if (!strcmp(a, "--windowed")) {
			vo.fullscreen = 0;
		} else if (!strcmp(a, "--no-ini")) {
		} else if (v && (!strcmp(a, "--scale") || !strcmp(a, "--fades") || !strcmp(a, "--aspect") ||
				   !strcmp(a, "--filter"))) {
			if (!set_option(&vo, a + 2, v)) {
				fprintf(stderr, "bad value %s for %s (see the comment at the top of main.c)\n", v, a);
				return 2;
			}
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
	if (!video_init(&vo)) {
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
