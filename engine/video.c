#include "video.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "dissolve_tables.h"
#include "god.h"
#include "runtime.h"
#include "script.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t pal[768];       /* palette on screen */
static uint32_t argb[SCREEN_W * SCREEN_H];
static MsRect clip = {0, 0, SCREEN_W, SCREEN_H};
/* rectangles of the clip region of the current redraw (NULL: only "clip") */
static const MsRect *clip_list;
static int nclip_list;
static MsValue on_sync_args[16];
static int on_sync_n;
static int stretch;       /* VIDEO_ASPECT_STRETCH: no logical size, the frame fills the window */
static int instant_fades; /* see VideoOptions */

#define DEFAULT_FADE_TIME 0.5 /* LIONKING.EXE DS:2F12 */
#define MAX_FADE_TIME 5.0     /* LIONKING.EXE DS:2F0A */
#define DEFAULT_START 1
#define DEFAULT_COUNT 254

int video_init(const VideoOptions *opt) {
	int scale = opt->scale < 1 ? 1 : opt->scale;
	stretch = opt->aspect == VIDEO_ASPECT_STRETCH;
	instant_fades = opt->instant_fades;
	window = SDL_CreateWindow("Il Re Leone - Libro Animato", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, SCREEN_W * scale, SCREEN_H * scale,
		SDL_WINDOW_RESIZABLE | (opt->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	if (!window)
		return 0;
	/* system palette static colours: entry 0 black, entry 255 white (see video_set_palette) */
	memset(pal + 255 * 3, 255, 3);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
		return 0;
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, opt->smooth ? "1" : "0");
	/* 4:3 and integer: black bars, mouse events in game pixels (see video_event_to_game);
	 * stretched: the texture is copied over the whole window */
	if (!stretch) {
		SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
		SDL_RenderSetIntegerScale(renderer, opt->aspect == VIDEO_ASPECT_INTEGER);
	}
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		SCREEN_W, SCREEN_H);
	return texture != NULL;
}

void video_shutdown(void) {
	if (texture)
		SDL_DestroyTexture(texture);
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	texture = NULL;
	renderer = NULL;
	window = NULL;
}

void video_toggle_fullscreen(void) {
	Uint32 f = SDL_GetWindowFlags(window);
	SDL_SetWindowFullscreen(window, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static void stretched_to_game(int *x, int *y) {
	int ww, wh;
	SDL_GetWindowSize(window, &ww, &wh);
	if (ww > 0 && wh > 0) {
		*x = (int)floor((double)*x * SCREEN_W / ww);
		*y = (int)floor((double)*y * SCREEN_H / wh);
	}
}

void video_event_to_game(int *x, int *y) {
	if (stretch)
		stretched_to_game(x, y);
}

void video_window_to_game(int wx, int wy, int *gx, int *gy) {
	int x = wx, y = wy;
	if (stretch) {
		stretched_to_game(&x, &y);
	} else {
		float lx, ly;
		SDL_RenderWindowToLogical(renderer, wx, wy, &lx, &ly);
		x = (int)lx;
		y = (int)ly;
	}
	*gx = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
	*gy = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
}

void video_set_clip(MsRect r) {
	int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
	int x1 = r.x + r.w > SCREEN_W ? SCREEN_W : r.x + r.w;
	int y1 = r.y + r.h > SCREEN_H ? SCREEN_H : r.y + r.h;
	clip.x = (int16_t)x0;
	clip.y = (int16_t)y0;
	clip.w = (int16_t)(x1 > x0 ? x1 - x0 : 0);
	clip.h = (int16_t)(y1 > y0 ? y1 - y0 : 0);
}

void video_set_clip_list(const MsRect *r, int n) {
	clip_list = r;
	nclip_list = n;
}

/* Runs body for each rectangle of the clip region in order, with "clip" set to it, as every
 * blit routine of LIONKING.EXE walks the region's rectangle list from the first (e.g.
 * 1188:3da4, 1188:40be, 10b8:030e, 10b8:03c0, 10b8:010e). */
#define FOR_EACH_CLIP(body) \
	do { \
		if (!clip_list) { \
			body; \
		} else { \
			MsRect saved_ = clip; \
			for (int ci_ = 0; ci_ < nclip_list; ci_++) { \
				video_set_clip(clip_list[ci_]); \
				body; \
			} \
			clip = saved_; \
		} \
	} while (0)

void video_present(void) {
	uint32_t lut[256];
	for (int i = 0; i < 256; i++)
		lut[i] = 0xff000000u | (uint32_t)pal[i * 3] << 16 | (uint32_t)pal[i * 3 + 1] << 8 | pal[i * 3 + 2];
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
		argb[i] = lut[fb[i]];
	SDL_UpdateTexture(texture, NULL, argb, SCREEN_W * 4);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

int video_save_bmp(const char *path) {
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 24, SDL_PIXELFORMAT_RGB24);
	if (!s)
		return 0;
	for (int y = 0; y < SCREEN_H; y++) {
		uint8_t *row = (uint8_t *)s->pixels + y * s->pitch;
		for (int x = 0; x < SCREEN_W; x++) {
			uint8_t c = fb[y * SCREEN_W + x];
			row[x * 3] = pal[c * 3];
			row[x * 3 + 1] = pal[c * 3 + 1];
			row[x * 3 + 2] = pal[c * 3 + 2];
		}
	}
	int ok = SDL_SaveBMP(s, path) == 0;
	SDL_FreeSurface(s);
	return ok;
}

/* ---- drawing ---- */

/* Copies src (w*h, pitch) at (x,y) clipped to the current clip rectangle. */
static void blit(const uint8_t *src, int w, int h, int pitch, int x, int y, int transparent) {
	int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
	if (x0 < clip.x)
		x0 = clip.x;
	if (y0 < clip.y)
		y0 = clip.y;
	if (x1 > clip.x + clip.w)
		x1 = clip.x + clip.w;
	if (y1 > clip.y + clip.h)
		y1 = clip.y + clip.h;
	if (x0 >= x1 || y0 >= y1)
		return;
	for (int yy = y0; yy < y1; yy++) {
		const uint8_t *s = src + (size_t)(yy - y) * pitch + (x0 - x);
		uint8_t *d = fb + yy * SCREEN_W + x0;
		int n = x1 - x0;
		if (!transparent) {
			memcpy(d, s, (size_t)n);
		} else {
			for (int i = 0; i < n; i++)
				if (s[i])
					d[i] = s[i];
		}
	}
}

int video_load_dissolve_tables(const char *exe_path) {
	return dissolve_tables_load(exe_path);
}

/* Without LIONKING.EXE: a 4x4 Bayer matrix keeps the same proportion of pixels. */
static int bayer_keep(int x, int y, int percent) {
	static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	return (bayer[y & 3][x & 3] + 0.5) / 16.0 < percent / 100.0;
}

static int pos_mod(int v, int m) {
	v %= m;
	return v < 0 ? v + m : v;
}

/* Partial dissolve of an uncompressed bitmap (LIONKING.EXE 10b8:160e). The pattern starts at
 * the clipped corner relative to the image origin; the column index wraps at the pattern size
 * but is NOT reset at the start of each line (it carries on from the previous line). Source
 * colour 0 is never drawn, also for opaque bitmaps. */
static void dissolve_raw(const uint8_t *px, int w, int h, int pitch, int x, int y,
	const DissolvePattern *pt, int percent) {
	int x0 = x > clip.x ? x : clip.x, y0 = y > clip.y ? y : clip.y;
	int x1 = x + w < clip.x + clip.w ? x + w : clip.x + clip.w;
	int y1 = y + h < clip.y + clip.h ? y + h : clip.y + clip.h;
	if (x0 >= x1 || y0 >= y1)
		return;
	int col = pt ? pos_mod(x0 - x, pt->size) : 0, row = pt ? pos_mod(y0 - y, pt->size) : 0;
	for (int dy = y0; dy < y1; dy++) {
		const uint8_t *s = px + (size_t)(dy - y) * pitch + (x0 - x);
		uint8_t *d = fb + dy * SCREEN_W + x0;
		for (int dx = x0; dx < x1; dx++, s++, d++) {
			int keep = pt ? pt->data[row * pt->size + col] == pt->threshold : bayer_keep(dx, dy, percent);
			if (keep && *s)
				*d = *s;
			if (pt && ++col >= pt->size)
				col = 0;
		}
		if (pt && ++row >= pt->size)
			row = 0;
	}
}

/* Partial dissolve of an RLE8 bitmap straight from the compressed data (LIONKING.EXE
 * 10b8:0ecc). Here the pattern is aligned to the screen: each run restarts at column
 * (screen x of the run) % size, also when its first pixels are clipped, and the column wraps
 * only after passing "size" (the original compares with > instead of >=, so column "size"
 * is used once per period). The row is (screen y) % size. Keyframe copies (0,2) are
 * skipped like transparent areas (0,3), as in the original. */
static void dissolve_rle(const MsBitmap *b, int x, int y, const DissolvePattern *pt) {
	const uint8_t *p = b->data, *end = b->data + b->data_size;
	int cx = x, cy = y;
	int left = clip.x, right = clip.x + clip.w, top = clip.y, bottom = clip.y + clip.h;
	while (p + 1 < end) {
		if (cy >= bottom)
			return;
		uint8_t op = p[0], arg = p[1];
		p += 2;
		if (op == 0 && arg == 0) {
			cx = x;
			cy++;
			continue;
		}
		if (op == 0 && arg == 1)
			return;
		if (op == 0 && (arg == 2 || arg == 3)) {
			if (p + 1 >= end)
				return;
			cx += p[0];
			cy += p[1];
			p += 2;
			continue;
		}
		int literal = op == 0, n = literal ? arg : op;
		const uint8_t *src = p;
		if (literal) {
			p += n + (n & 1);
			if (p > end)
				return;
		}
		int run_x = cx;
		cx += n;
		if (cy < top)
			continue;
		int first = run_x < left ? left - run_x : 0;
		int last = cx > right ? n - (cx - right) : n;
		int col = pos_mod(run_x, pt->size), row = pos_mod(cy, pt->size);
		uint8_t *d = fb + cy * SCREEN_W + run_x;
		for (int i = first; i < last; i++) {
			uint8_t c = literal ? src[i] : arg;
			/* colour 0 is drawn: only skips and line ends are transparent */
			if (pt->data[row * pt->size + col] == pt->threshold)
				d[i] = c;
			if (++col > pt->size)
				col = 0;
		}
	}
}

/* Ordinary blit of an RLE8 bitmap straight from the compressed data, clipped to the current
 * clip rectangle (LIONKING.EXE 10a0:0000 -> 1188:40be, which calls 10e8:09ee -> 1118:0024 for
 * each dirty rectangle). Runs and literal pixels are copied as they are, colour 0 included:
 * only skips (0,3), keyframe copies (0,2) and the rest of a line after an end of line are
 * transparent. */
static void blit_rle(const MsBitmap *b, int x, int y) {
	bitmap_rle8_blit(b, fb, SCREEN_W, x, y, clip.x, clip.y, clip.x + clip.w, clip.y + clip.h);
}

static uint8_t *decode_rle(const MsBitmap *b) {
	uint8_t *buf = calloc((size_t)b->w * b->h, 1);
	if (b->data)
		bitmap_rle8_decode(b, buf, b->w);
	return buf;
}

void rt_draw_bitmap(const MsBitmap *b, int x, int y, double dissolve) {
	/* The original rounds the factor to a percentage first (LIONKING.EXE 10a0:0035):
	 * 0 % draws nothing, 100 % is an ordinary blit, anything else a pattern. */
	int percent = dissolve_percent(dissolve);
	if (!b || b->w <= 0 || b->h <= 0 || percent <= 0)
		return;
	const uint8_t *px;
	int pitch, transparent;
	uint8_t *tmp = NULL;
	/* LIONKING.EXE 10a0:0000 picks the routine from the compression: 1 RLE8, 5 CCC, 6 CCC
	 * transparent, 7 uncompressed transparent, any other value uncompressed opaque. CCC is
	 * drawn only at 100 % (10b8:1840, 10b8:010e); a partial dissolve of a CCC image draws
	 * nothing. */
	switch (b->compression) {
	case BMP_RLE8:
		tmp = b->decoded || (percent >= 100 && b->data) ? NULL : decode_rle(b);
		px = b->decoded ? b->decoded : tmp;
		pitch = b->w;
		transparent = 1;
		break;
	case BMP_CCC:
		/* 10b8:1840 is called once per draw and ignores the region rectangles: only the
		 * screen limits the drawing here. Since the stage draws each actor once over the
		 * whole region, an actor on top is drawn after it, as in the original. */
		if (percent >= 100 && b->data)
			bitmap_ccc_blit(b, fb, SCREEN_W, x, y, 0, 0, SCREEN_W, SCREEN_H);
		return;
	case BMP_CCC_TRANSPARENT:
		/* 10b8:010e -> 10b8:19f6 for each dirty rectangle */
		if (percent >= 100 && b->data)
			FOR_EACH_CLIP(bitmap_ccc_transparent_blit(b, fb, SCREEN_W, x, y, clip.x, clip.y,
				clip.x + clip.w, clip.y + clip.h));
		return;
	default:
		if (!b->pixels)
			return;
		px = b->pixels;
		pitch = b->stride;
		transparent = b->compression == BMP_RAW_TRANSPARENT;
		break;
	}
	if (percent >= 100 && b->compression == BMP_RLE8 && b->data) {
		FOR_EACH_CLIP(blit_rle(b, x, y));
	} else if (percent >= 100) {
		FOR_EACH_CLIP(blit(px, b->w, b->h, pitch, x, y, transparent));
	} else {
		const DissolvePattern *pt = dissolve_loaded ? &dissolve_patterns[dissolve_index(percent)] : NULL;
		/* the pattern phase restarts in each rectangle of the region (see dissolve_raw) */
		if (pt && b->compression == BMP_RLE8 && b->data)
			FOR_EACH_CLIP(dissolve_rle(b, x, y, pt));
		else
			FOR_EACH_CLIP(dissolve_raw(px, b->w, b->h, pitch, x, y, pt, percent));
	}
	free(tmp);
}

void rt_draw_delta(const MsBitmap *delta, const MsBitmap *key, int x, int y, int key_dx, int key_dy) {
	if (!delta || delta->w <= 0 || delta->h <= 0)
		return;
	const uint8_t *keypx = NULL;
	int kw = 0, kh = 0;
	if (key) {
		kw = key->w;
		kh = key->h;
		if (key->compression == BMP_RLE8) {
			/* keyframes are decoded once and kept (decompressIntoAuxImage in the original) */
			MsBitmap *k = (MsBitmap *)key;
			if (!k->decoded)
				k->decoded = decode_rle(k);
			keypx = k->decoded;
		} else if (key->pixels && key->stride == key->w) {
			keypx = key->pixels;
		} else if (key->pixels) {
			MsBitmap *k = (MsBitmap *)key;
			if (!k->decoded) {
				k->decoded = malloc((size_t)kw * kh);
				for (int r = 0; r < kh; r++)
					memcpy(k->decoded + r * kw, key->pixels + r * key->stride, (size_t)kw);
			}
			keypx = k->decoded;
		}
	}
	uint8_t *buf = calloc((size_t)delta->w * delta->h, 1);
	if (delta->compression == BMP_RLE8)
		bitmap_rle8_decode_delta(delta, buf, delta->w, keypx, kw, kh, key_dx, key_dy);
	else if (delta->pixels)
		for (int r = 0; r < delta->h; r++)
			memcpy(buf + r * delta->w, delta->pixels + r * delta->stride, (size_t)delta->w);
	FOR_EACH_CLIP(blit(buf, delta->w, delta->h, delta->w, x, y, 1));
	free(buf);
}

static void thick_line(int x0, int y0, int x1, int y1, int thickness, uint8_t colour);

void rt_draw_thick_line(int x0, int y0, int x1, int y1, int thickness, uint8_t colour) {
	FOR_EACH_CLIP(thick_line(x0, y0, x1, y1, thickness, colour));
}

static void thick_line(int x0, int y0, int x1, int y1, int thickness, uint8_t colour) {
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	int h = thickness / 2;
	for (;;) {
		for (int yy = y0 - h; yy < y0 - h + thickness; yy++)
			for (int xx = x0 - h; xx < x0 - h + thickness; xx++)
				if (xx >= clip.x && xx < clip.x + clip.w && yy >= clip.y && yy < clip.y + clip.h)
					fb[yy * SCREEN_W + xx] = colour;
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

uint8_t rt_find_color(uint8_t r, uint8_t g, uint8_t b) {
	int best = 0;
	long bestd = 1L << 30;
	for (int i = 0; i < 256; i++) {
		long dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
		long d = dr * dr + dg * dg + db * db;
		if (d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return (uint8_t)best;
}

/* ---- palette and transitions ---- */

static void limit_range(int *start, int *count) {
	if (*start < 0)
		*start = 0;
	if (*start > 255)
		*start = 255;
	int end = *start + *count;
	if (end > 256)
		end = 256;
	if (end < *start)
		end = *start;
	*count = end - *start;
}

/* The platform layer never writes the first and the last entry: AnimatePalette and
 * SetPaletteEntries get the range from max(start, 1) to min(start + count, 255), and the
 * full palette takes entries 0 and 255 from the system palette, i.e. the Windows static
 * colours black and white (LIONKING.EXE 1188:4c60, 1188:4d2c). */
void video_set_palette(const uint8_t *p, int start, int count) {
	limit_range(&start, &count);
	int first = start == 0 ? 1 : start, end = start + count > 255 ? 255 : start + count;
	if (end > first)
		memcpy(pal + first * 3, p + first * 3, (size_t)(end - first) * 3);
}

const uint8_t *video_palette(void) {
	return pal;
}

static int SDLCALL take_alt_enter(void *found, SDL_Event *ev) {
	if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_RETURN && (ev->key.keysym.mod & KMOD_ALT)) {
		*(int *)found = 1;
		return 0;
	}
	return 1;
}

/* A fade does not dispatch messages (1190:a9be), so input waits for its end; Alt+Enter, a
 * port addition, is taken out of the queue and served at once. */
static void present_pumping(void) {
	int alt_enter = 0;
	SDL_PumpEvents();
	SDL_FilterEvents(take_alt_enter, &alt_enter);
	if (alt_enter)
		video_toggle_fullscreen();
	video_present();
}

/* Palette fade as in LIONKING.EXE 1190:a9be: the step is k = _ftol(1024.0 * elapsed / time),
 * at most 1024, and each component is src + (dst - src) * k / 1024 with integer division
 * (truncated towards zero), clamped to 0..255. The original loops without waiting until
 * now >= start + time (a time <= 0 sets the target at once), then sets the exact target.
 * Fades longer than MAX_FADE_TIME are shortened (palette manager 1190:a37e, DS:2F0A).
 * On a screen without a palette (16/24 bit) the step setter +0x1c (1188:35b2 -> 1188:4c60)
 * returns 1 at once because [obj+0x0A] == 0, the loop of 1190:a9be stops and only the final
 * palette is set (+0x20, 1188:35ce): that is the "instant fades" option. */
static uint8_t fade_component(uint8_t s, uint8_t t, long k) {
	long r = s + ((long)t - s) * k / 1024;
	return (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
}

static void fade_to(const uint8_t *target, double time, int start, int count) {
	limit_range(&start, &count);
	if (time > MAX_FADE_TIME)
		time = MAX_FADE_TIME;
	if (instant_fades)
		time = 0;
	uint8_t from[768], mid[768];
	memcpy(from, pal, 768);
	memcpy(mid, pal, 768);
	double t0 = SDL_GetTicks() / 1000.0, end = t0 + time;
	for (;;) {
		double now = SDL_GetTicks() / 1000.0;
		if (!(now < end))
			break;
		long k = (long)(1024.0 * ((now - t0) / time));
		if (k > 1024)
			k = 1024;
		for (int i = start * 3; i < (start + count) * 3; i++)
			mid[i] = fade_component(from[i], target[i], k);
		video_set_palette(mid, start, count);
		present_pumping();
		SDL_Delay(5);
	}
	video_set_palette(target, start, count);
	present_pumping();
}

/* Arguments are read with the exact accessors of the original (1190:a6dc): the type as a
 * token (1190:4e80), the duration as a time (1190:4e26: any other type gives 0, an
 * immediate change), start and count as floats (1190:4dc0) truncated by _ftol. */
static double arg_time(MsValue *a, int n, int i, double def) {
	return i < n ? val_time_of(&a[i]) : def;
}

static void effect_transition(MsValue *args, int nargs) {
	if (nargs < 1)
		return;
	uint32_t type = val_token_of(&args[0]);
	uint8_t black[768];
	int start = DEFAULT_START, count = DEFAULT_COUNT;
	switch (type) {
	case 300: /* fade to black */
	case 301: /* fade to the registered palette */ {
		double t = arg_time(args, nargs, 1, DEFAULT_FADE_TIME);
		if (nargs >= 4) {
			start = (int)val_float_of(&args[2]);
			count = (int)val_float_of(&args[3]);
		}
		if (type == 300) {
			memset(black, 0, sizeof(black));
			fade_to(black, t, start, count);
		} else {
			fade_to(ms_god.palette, t, start, count);
		}
		break;
	}
	case 302: /* set to the registered palette */
		if (nargs >= 3) {
			start = (int)val_float_of(&args[1]);
			count = (int)val_float_of(&args[2]);
		}
		video_set_palette(ms_god.palette, start, count);
		break;
	default:
		/* LIONKING.EXE 1190:a6dc implements only 300, 301 and 302: every other type (the
		 * 303-328 of later Media Station titles included) does nothing. */
		break;
	}
}

/* Every EffectTransition, also one without arguments, ends by dropping the transition
 * pending "on sync" (LIONKING.EXE 1190:a6dc -> 1190:a680). */
static void clear_on_sync(void) {
	for (int i = 0; i < on_sync_n; i++)
		val_free(&on_sync_args[i]);
	on_sync_n = 0;
}

void video_effect_transition(MsValue *args, int nargs) {
	effect_transition(args, nargs);
	clear_on_sync();
}

void video_set_transition_on_sync(MsValue *args, int nargs) {
	clear_on_sync();
	for (int i = 0; i < nargs && i < 16; i++)
		on_sync_args[on_sync_n++] = val_copy(&args[i]);
}

void video_do_transition_on_sync(void) {
	if (on_sync_n == 0)
		return;
	MsValue args[16];
	int n = on_sync_n;
	memcpy(args, on_sync_args, sizeof(MsValue) * (size_t)n);
	on_sync_n = 0;
	video_effect_transition(args, n);
	for (int i = 0; i < n; i++)
		val_free(&args[i]);
}
