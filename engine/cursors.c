/* Cursors. The titles declare their cursors in BOOT.STM (command 0x15): a cursor id and either
 * a "platform" cursor (the system arrow) or the name of a cursor group resource of the game
 * executable (LIONKING.EXE, a Windows 3.1 NE file: NOCURSOR, ACTIVELK, INACTIVELK,
 * BUTTRFLYLK). Screens, hotspots and cursor actors then select cursors by id; the logic of
 * permanent/temporary cursors follows the original (CursorManager). */
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datafile.h"
#include "god.h"
#include "runtime.h"

#define MAX_CURSORS 64

typedef struct {
	uint16_t id;
	SDL_Cursor *cursor; /* NULL = system arrow */
	int hidden;         /* "NOCURSOR" style: fully transparent */
} Cursor;

static Cursor cursors[MAX_CURSORS];
static int ncursors;
static uint16_t permanent_id, current_id;
static SDL_Cursor *arrow;

static MsFile exe;
static int exe_loaded;

static uint16_t rd16(const uint8_t *p) {
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int cursors_load_exe(const char *path) {
	exe_loaded = ms_file_load(&exe, path);
	if (exe_loaded && (exe.size < 0x40 || rd16(exe.data + rd32(exe.data + 0x3c)) != 0x454e)) {
		ms_warn("%s is not a Windows 3.1 (NE) executable", path);
		ms_file_free(&exe);
		exe_loaded = 0;
	}
	arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	return exe_loaded;
}

/* Finds a resource of the NE file by type and name (or numeric id if name is NULL). */
static const uint8_t *ne_resource(uint16_t type, const char *name, uint16_t num, uint32_t *len) {
	if (!exe_loaded)
		return NULL;
	const uint8_t *d = exe.data;
	uint32_t ne = rd32(d + 0x3c);
	uint32_t rt = ne + rd16(d + ne + 0x24);
	uint16_t shift = rd16(d + rt);
	uint32_t p = rt + 2;
	while (p + 8 <= exe.size) {
		uint16_t t = rd16(d + p);
		if (t == 0)
			break;
		uint16_t count = rd16(d + p + 2);
		p += 8;
		for (int i = 0; i < count; i++, p += 12) {
			if (t != (0x8000 | type))
				continue;
			uint16_t off = rd16(d + p), ln = rd16(d + p + 2), rid = rd16(d + p + 6);
			int match = 0;
			if (name && !(rid & 0x8000)) {
				const uint8_t *s = d + rt + rid;
				size_t n = s[0];
				if (strlen(name) == n) {
					match = 1;
					for (size_t k = 0; k < n; k++)
						if (toupper(s[1 + k]) != toupper((unsigned char)name[k]))
							match = 0;
				}
			} else if (!name && (rid & 0x8000)) {
				match = (rid & 0x7fff) == num;
			}
			if (match) {
				*len = (uint32_t)ln << shift;
				uint32_t o = (uint32_t)off << shift;
				return o + *len <= exe.size ? d + o : NULL;
			}
		}
	}
	return NULL;
}

/* Win16 cursor: hotspot (2 x u16) then a monochrome DIB whose height covers XOR + AND masks. */
static SDL_Cursor *make_cursor(const char *group_name, int *hidden) {
	uint32_t len;
	const uint8_t *g = ne_resource(12, group_name, 0, &len); /* RT_GROUP_CURSOR */
	if (!g || len < 6 + 14)
		return NULL;
	/* first entry of the group: ..., the last u16 is the cursor resource id */
	uint16_t id = rd16(g + 6 + 12);
	const uint8_t *c = ne_resource(1, NULL, id, &len); /* RT_CURSOR */
	if (!c || len < 4 + 40)
		return NULL;
	int hx = rd16(c), hy = rd16(c + 2);
	const uint8_t *bih = c + 4;
	int w = (int)rd32(bih + 4), h2 = (int)rd32(bih + 8);
	int bpp = rd16(bih + 14);
	int h = h2 / 2;
	if (bpp != 1 || w <= 0 || h <= 0 || w > 64 || h > 64)
		return NULL;
	const uint8_t *xor_bits = bih + 40 + 8; /* two palette entries */
	int stride = ((w + 31) / 32) * 4;
	const uint8_t *and_bits = xor_bits + stride * h;
	if (and_bits + stride * h > c + len)
		return NULL;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	int visible = 0;
	for (int y = 0; y < h; y++) {
		/* DIB rows are bottom-up */
		const uint8_t *xr = xor_bits + (h - 1 - y) * stride;
		const uint8_t *ar = and_bits + (h - 1 - y) * stride;
		uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
		for (int x = 0; x < w; x++) {
			int a = (ar[x >> 3] >> (7 - (x & 7))) & 1;
			int xo = (xr[x >> 3] >> (7 - (x & 7))) & 1;
			uint32_t px;
			if (!a)
				px = xo ? 0xffffffffu : 0xff000000u;
			else
				px = xo ? 0xff000000u : 0x00000000u; /* inverted pixels shown black */
			if (px >> 24)
				visible = 1;
			row[x] = px;
		}
	}
	*hidden = !visible;
	SDL_Cursor *cur = SDL_CreateColorCursor(s, hx, hy);
	SDL_FreeSurface(s);
	return cur;
}

/* Platform cursors (type 0, LIONKING.EXE 11a0:0a3a, vtable 11a0:0b58): LoadCursor(NULL,
 * platform id) (11a0:0ac4), i.e. a Windows system cursor (IDC_*); SetCursor at selection
 * (11a0:0b16). Here the matching SDL system cursor. */
static SDL_Cursor *system_cursor(uint16_t idc) {
	SDL_SystemCursor c;
	switch (idc) {
	case 32512: c = SDL_SYSTEM_CURSOR_ARROW; break;     /* IDC_ARROW */
	case 32513: c = SDL_SYSTEM_CURSOR_IBEAM; break;     /* IDC_IBEAM */
	case 32514: c = SDL_SYSTEM_CURSOR_WAIT; break;      /* IDC_WAIT */
	case 32515: c = SDL_SYSTEM_CURSOR_CROSSHAIR; break; /* IDC_CROSS */
	case 32516: c = SDL_SYSTEM_CURSOR_ARROW; break;     /* IDC_UPARROW: no SDL equivalent */
	case 32640: c = SDL_SYSTEM_CURSOR_SIZEALL; break;   /* IDC_SIZE */
	case 32641: c = SDL_SYSTEM_CURSOR_ARROW; break;     /* IDC_ICON: no SDL equivalent */
	case 32642: c = SDL_SYSTEM_CURSOR_SIZENWSE; break;  /* IDC_SIZENWSE */
	case 32643: c = SDL_SYSTEM_CURSOR_SIZENESW; break;  /* IDC_SIZENESW */
	case 32644: c = SDL_SYSTEM_CURSOR_SIZEWE; break;    /* IDC_SIZEWE */
	case 32645: c = SDL_SYSTEM_CURSOR_SIZENS; break;    /* IDC_SIZENS */
	default: return NULL;
	}
	return SDL_CreateSystemCursor(c);
}

/* command 0x15 (cursor manager 1190:659a → 1190:62b2): only an id inside the range of
 * command 0x0c and not already defined; if the cursor cannot be loaded the id stays free */
void cursors_new(uint16_t id, int type, uint16_t platform_id, const char *resource) {
	if (id < ms_god.cursor_base || id > ms_god.cursor_max) {
		ms_warn("cursor id %u out of the declared range: ignored", id);
		return;
	}
	for (int i = 0; i < ncursors; i++)
		if (cursors[i].id == id)
			return;
	if (ncursors == MAX_CURSORS)
		return;
	Cursor *c = &cursors[ncursors];
	memset(c, 0, sizeof(*c));
	c->id = id;
	if (type == 0) {
		c->cursor = system_cursor(platform_id);
		if (!c->cursor) {
			ms_warn("platform cursor %u (IDC %u) unknown", id, platform_id);
			return;
		}
	} else if (resource) {
		c->cursor = make_cursor(resource, &c->hidden);
		if (!c->cursor && !c->hidden) {
			ms_warn("cursor %s not found in the game executable", resource);
			return;
		}
	}
	ncursors++;
}

/* command 0x16 (1190:6516): the current and the permanent cursor are never disposed */
void cursors_dispose(uint16_t id) {
	if (id == current_id || id == permanent_id)
		return;
	for (int i = 0; i < ncursors; i++)
		if (cursors[i].id == id) {
			if (cursors[i].cursor)
				SDL_FreeCursor(cursors[i].cursor);
			cursors[i] = cursors[--ncursors];
			return;
		}
}

static void reset_current(void) {
	if (current_id == 0)
		return;
	for (int i = 0; i < ncursors; i++)
		if (cursors[i].id == current_id) {
			if (cursors[i].hidden) {
				SDL_ShowCursor(SDL_DISABLE);
			} else {
				SDL_ShowCursor(SDL_ENABLE);
				SDL_SetCursor(cursors[i].cursor ? cursors[i].cursor : arrow);
			}
			return;
		}
	ms_warn("cursor %u not found", current_id);
}

void rt_cursor_register_permanent(uint16_t id) {
	if (id)
		permanent_id = id;
}

void rt_cursor_set_permanent(uint16_t id) {
	/* 1190:67be: only when it changes and the id is inside the declared range */
	if ((current_id == id && permanent_id == id) || id == 0 || id < ms_god.cursor_base ||
	    id > ms_god.cursor_max)
		return;
	permanent_id = id;
	current_id = id;
	reset_current();
}

void rt_cursor_set_temporary(uint16_t id) {
	if (current_id == id || id == 0)
		return;
	current_id = id;
	reset_current();
}

void rt_cursor_unset_temporary(void) {
	if (current_id != permanent_id) {
		current_id = permanent_id;
		reset_current();
	}
}

/* the window is active again (LIONKING.EXE 1188:033c calls the cursor manager, DS:033A
 * vtable +4, after a focus loss): show the current cursor again */
void cursors_refresh(void) {
	reset_current();
}

/* ============================================================ the "About" box (Ctrl+V)
 * WM_CHAR 0x16 opens dialog 1 of the executable (DialogBox, procedure 1188:5874); on
 * WM_INITDIALOG 1188:56be fills it from the string table and the VERSIONINFO resource of the
 * running executable. Everything is read here from LIONKING.EXE (--exe); nothing of the
 * texts is in the port. */

/* a string of a Win16 DLGTEMPLATE: ordinal (0xff, u16) or zero-terminated */
static const uint8_t *dlg_string(const uint8_t *p, const uint8_t *end, char *out, size_t cap) {
	size_t n = 0;
	if (p < end && *p == 0xff) /* ordinal */
		return p + 3 <= end ? p + 3 : end;
	while (p < end && *p) {
		if (out && n + 1 < cap)
			out[n++] = (char)*p;
		p++;
	}
	if (out && cap)
		out[n] = 0;
	return p < end ? p + 1 : end;
}

/* Win16 LoadString: string `id` of the RT_STRING table (block id/16 + 1, entry id % 16, each a
 * length byte and the characters); a missing string is empty, like LoadString returning 0. */
static void ne_string(uint16_t id, char *out, size_t cap) {
	uint32_t len;
	const uint8_t *b = ne_resource(6, NULL, (uint16_t)((id >> 4) + 1), &len);
	out[0] = 0;
	if (!b)
		return;
	uint32_t p = 0;
	for (int i = 0; i < (id & 15) && p < len; i++)
		p += 1u + b[p];
	if (p >= len)
		return;
	size_t n = b[p];
	if (p + 1 + n > len)
		n = len - p - 1;
	if (n >= cap)
		n = cap - 1;
	memcpy(out, b + p + 1, n);
	out[n] = 0;
}

/* 1188:0ce4: string 1 compared (_fstricmp, 1180:0640) with the language names at 1188:1952
 * gives the offset of the language's strings; unknown names give 0 */
static int about_language(void) {
	static const char *names[] = {"ENGLISH", "GERMAN", "FRENCH", "ITALIAN", "DUTCH", "CASTILIAN", "BRITISH"};
	static const int offsets[] = {0, 250, 500, 750, 1000, 1250, 1500};
	char s[256];
	ne_string(1, s, sizeof(s));
	for (int i = 0; i < 7; i++)
		if (!SDL_strcasecmp(s, names[i]))
			return offsets[i];
	return 0;
}

/* 1188:52d2: GetFileVersionInfo of the module file, VerQueryValue
 * "\StringFileInfo\040904E4\FileVersion" (1188:58c0); empty if missing. Win16 version blocks:
 * u16 total size, u16 value size, the key (ANSI, padded to 4 bytes), the value (padded), then
 * the children. */
static int ver_find(const uint8_t *d, uint32_t len, const char *const *path, int depth, char *out, size_t cap) {
	uint32_t p = 0;
	while (p + 4 < len) {
		uint32_t size = rd16(d + p), vsize = rd16(d + p + 2);
		if (size < 4 || p + size > len)
			return 0;
		const char *key = (const char *)d + p + 4;
		size_t klen = strnlen(key, size - 4);
		uint32_t v = (4 + (uint32_t)klen + 1 + 3) & ~3u;
		if (!SDL_strcasecmp(key, path[depth]) && klen < size - 4) {
			if (!path[depth + 1]) {
				size_t n = vsize;
				if (v + n > size)
					n = size > v ? size - v : 0;
				n = strnlen((const char *)d + p + v, n);
				if (n >= cap)
					n = cap - 1;
				memcpy(out, d + p + v, n);
				out[n] = 0;
				return 1;
			}
			uint32_t c = v + ((vsize + 3) & ~3u);
			return c < size && ver_find(d + p + c, size - c, path, depth + 1, out, cap);
		}
		p += (size + 3) & ~3u;
	}
	return 0;
}

static void about_file_version(char *out, size_t cap) {
	static const char *const path[] = {"VS_VERSION_INFO", "StringFileInfo", "040904E4", "FileVersion", NULL};
	uint32_t len;
	const uint8_t *d = ne_resource(16, NULL, 1, &len); /* RT_VERSION */
	out[0] = 0;
	if (!d)
		return;
	/* the root block: its children start after the fixed VS_FIXEDFILEINFO value */
	uint32_t size = rd16(d), vsize = rd16(d + 2);
	if (size > len || size < 4 || SDL_strcasecmp((const char *)d + 4, path[0]))
		return;
	uint32_t c = ((4 + (uint32_t)strlen(path[0]) + 1 + 3) & ~3u) + ((vsize + 3) & ~3u);
	if (c < size)
		ver_find(d + c, size - c, path, 1, out, cap);
}

/* 1188:51d0: the first "%T" of the format is replaced by the argument */
static void about_format(const char *fmt, const char *arg, char *out, size_t cap) {
	const char *t = strstr(fmt, "%T");
	if (!t)
		snprintf(out, cap, "%s", fmt);
	else
		snprintf(out, cap, "%.*s%s%s", (int)(t - fmt), fmt, arg, t + 2);
}

/* The strings are Windows ANSI (code page 1252); SDL wants UTF-8. */
static void append_utf8(char *out, size_t cap, const char *s) {
	static const uint16_t cp1252[32] = {0x20ac, 0, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030,
		0x160, 0x2039, 0x152, 0, 0x17d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc,
		0x2122, 0x161, 0x203a, 0x153, 0, 0x17e, 0x178};
	size_t n = strlen(out);
	for (; *s; s++) {
		unsigned c = (unsigned char)*s;
		if (c >= 0x80 && c < 0xa0)
			c = cp1252[c - 0x80] ? cp1252[c - 0x80] : '?';
		char b[3];
		int k;
		if (c < 0x80) {
			b[0] = (char)c;
			k = 1;
		} else if (c < 0x800) {
			b[0] = (char)(0xc0 | c >> 6);
			b[1] = (char)(0x80 | (c & 0x3f));
			k = 2;
		} else {
			b[0] = (char)(0xe0 | c >> 12);
			b[1] = (char)(0x80 | ((c >> 6) & 0x3f));
			b[2] = (char)(0x80 | (c & 0x3f));
			k = 3;
		}
		if (n + (size_t)k + 1 > cap)
			break;
		memcpy(out + n, b, (size_t)k);
		n += (size_t)k;
		out[n] = 0;
	}
}

typedef struct {
	int y, x;
	char s[256];
} AboutLine;

/* Fills the box like 1188:56be. Returns 0 without the executable or its dialog 1. Layout of
 * dialog 1: static items 3, 6, 7, 4, 5 from the top and button 1. */
int cursors_about_box(char *title, size_t title_cap, char *text, size_t cap, char *button, size_t button_cap) {
	uint32_t len;
	const uint8_t *d = ne_resource(5, NULL, 1, &len); /* RT_DIALOG */
	if (!d || len < 13)
		return 0;
	int lang = about_language();
	char s[256], s2[256];
	AboutLine lines[32];
	int n = 0;

	/* the item positions of the dialog template (DLGTEMPLATE, items DLGITEMTEMPLATE) */
	const uint8_t *end = d + len, *p = d;
	uint32_t style = rd32(p);
	int items = p[4];
	p += 5 + 8;
	p = dlg_string(p, end, NULL, 0); /* menu */
	p = dlg_string(p, end, NULL, 0); /* class */
	p = dlg_string(p, end, NULL, 0); /* caption: replaced below */
	if (style & 0x40) {              /* DS_SETFONT */
		p += 2;
		p = dlg_string(p, end, NULL, 0);
	}
	int ypos[8] = {0}, xpos[8] = {0}, height[8] = {0};
	for (int i = 0; i < items && p + 14 <= end; i++) {
		int x = (int16_t)rd16(p), y = (int16_t)rd16(p + 2), h = (int16_t)rd16(p + 6), id = rd16(p + 8);
		p += 14;
		if (p < end && (*p & 0x80))
			p++;
		else
			p = dlg_string(p, end, NULL, 0);
		p = dlg_string(p, end, NULL, 0);
		if (p < end)
			p += 1 + *p;
		if (id < 8) {
			xpos[id] = x;
			ypos[id] = y;
			height[id] = h;
		}
	}

	/* caption: string 0xb1 of the language, " " (1188:58ee), string 0x10 (the title) */
	ne_string((uint16_t)(lang + 0xb1), s, sizeof(s));
	ne_string(0x10, s2, sizeof(s2));
	title[0] = 0;
	append_utf8(title, title_cap, s);
	append_utf8(title, title_cap, " ");
	append_utf8(title, title_cap, s2);

	/* item 3: string 0x10 (no language offset) */
	lines[n].y = ypos[3], lines[n].x = xpos[3];
	ne_string(0x10, lines[n++].s, sizeof(lines[0].s));
	/* 1188:5410: item 7 = string 0xb6; if string 0xb7 (without the language offset) is not
	 * empty, one more line per string below item 7: first that very string 0xb7, then the
	 * language's 0xb8, 0xb9, ... up to an empty one; 1188:5596 then moves items 4, 5 and 1
	 * down by as many lines (the height of item 4) */
	lines[n].y = ypos[7], lines[n].x = xpos[7];
	ne_string((uint16_t)(lang + 0xb6), lines[n++].s, sizeof(lines[0].s));
	int extra = 0;
	ne_string(0xb7, s, sizeof(s));
	while (s[0] && n < 28) {
		extra++;
		lines[n].y = ypos[7] + extra * height[7], lines[n].x = xpos[7];
		snprintf(lines[n++].s, sizeof(lines[0].s), "%s", s);
		ne_string((uint16_t)(lang + 0xb7 + extra), s, sizeof(s));
	}
	/* items 4 and 5: strings 0xb2, 0xb3 */
	lines[n].y = ypos[4] + extra * height[4], lines[n].x = xpos[4];
	ne_string((uint16_t)(lang + 0xb2), lines[n++].s, sizeof(lines[0].s));
	lines[n].y = ypos[5] + extra * height[4], lines[n].x = xpos[5];
	ne_string((uint16_t)(lang + 0xb3), lines[n++].s, sizeof(lines[0].s));
	/* item 6: string 0xb4 with "%T" = the file version (1188:52d2, 1188:51d0) */
	ne_string((uint16_t)(lang + 0xb4), s, sizeof(s));
	about_file_version(s2, sizeof(s2));
	lines[n].y = ypos[6], lines[n].x = xpos[6];
	about_format(s, s2, lines[n++].s, sizeof(lines[0].s));
	/* button 1: string 0xb5 */
	ne_string((uint16_t)(lang + 0xb5), s, sizeof(s));
	button[0] = 0;
	append_utf8(button, button_cap, s);

	/* the message box shows the static items from the top */
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && (lines[j].y < lines[j - 1].y ||
		                          (lines[j].y == lines[j - 1].y && lines[j].x < lines[j - 1].x));
		     j--) {
			AboutLine t = lines[j];
			lines[j] = lines[j - 1];
			lines[j - 1] = t;
		}
	text[0] = 0;
	for (int i = 0; i < n; i++) {
		if (i)
			append_utf8(text, cap, "\n");
		append_utf8(text, cap, lines[i].s);
	}
	return 1;
}
