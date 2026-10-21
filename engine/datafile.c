#include "datafile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ms_verbose = 0;

void ms_fatal(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("!! ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	fflush(stdout);
	exit(1);
}

void ms_warn(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("?? ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

int ms_file_load(MsFile *f, const char *path) {
	memset(f, 0, sizeof(*f));
	snprintf(f->path, sizeof(f->path), "%s", path);
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return 0;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	f->data = malloc(n > 0 ? (size_t)n : 1);
	if (!f->data || fread(f->data, 1, (size_t)n, fp) != (size_t)n) {
		fclose(fp);
		free(f->data);
		f->data = NULL;
		return 0;
	}
	fclose(fp);
	f->size = (size_t)n;
	return 1;
}

void ms_file_free(MsFile *f) {
	free(f->data);
	f->data = NULL;
	f->size = 0;
}

const char *ms_tag_str(uint32_t tag) {
	static char buf[8][8];
	static int n;
	char *s = buf[n++ & 7];
	for (int i = 0; i < 4; i++) {
		char ch = (char)(tag >> (24 - 8 * i));
		s[i] = (ch >= 32 && ch < 127) ? ch : '?';
	}
	s[4] = 0;
	return s;
}

static void need(const MsChunk *c, size_t n) {
	if (c->pos + n > c->end)
		ms_fatal("read of %u bytes past the end of chunk %s (@0x%zx, end 0x%zx)", (unsigned)n,
			ms_tag_str(c->id), c->pos, c->end);
}

size_t ms_remaining(const MsChunk *c) {
	return c->pos < c->end ? c->end - c->pos : 0;
}

uint8_t ms_u8(MsChunk *c) {
	need(c, 1);
	return c->base[c->pos++];
}

uint16_t ms_u16(MsChunk *c) {
	need(c, 2);
	const uint8_t *p = c->base + c->pos;
	c->pos += 2;
	return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t ms_u32(MsChunk *c) {
	need(c, 4);
	const uint8_t *p = c->base + c->pos;
	c->pos += 4;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t ms_u32be(MsChunk *c) {
	need(c, 4);
	const uint8_t *p = c->base + c->pos;
	c->pos += 4;
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

int16_t ms_s16(MsChunk *c) {
	return (int16_t)ms_u16(c);
}

void ms_read(MsChunk *c, void *dst, size_t n) {
	need(c, n);
	memcpy(dst, c->base + c->pos, n);
	c->pos += n;
}

void ms_skip(MsChunk *c, size_t n) {
	need(c, n);
	c->pos += n;
}

const uint8_t *ms_ptr(const MsChunk *c) {
	return c->base + c->pos;
}

/* ---- subfiles ---- */

static MsChunk raw_chunk(const MsFile *f, size_t pos, size_t limit) {
	MsChunk c = {f->data, pos, limit, 0, pos};
	c.id = ms_u32be(&c);
	uint32_t len = ms_u32(&c);
	c.start = c.pos;
	c.end = c.pos + len;
	if (c.end > limit)
		ms_fatal("%s: chunk %s @0x%zx (len 0x%x) exceeds its container", f->path,
			ms_tag_str(c.id), pos, len);
	return c;
}

size_t ms_subfile_open(MsSubfile *sf, const MsFile *f, size_t pos) {
	memset(sf, 0, sizeof(*sf));
	if (pos & 1)
		pos++;
	sf->root = raw_chunk(f, pos, f->size);
	if (sf->root.id != MS_TAG('R', 'I', 'F', 'F'))
		ms_fatal("%s: expected RIFF @0x%zx, got %s", f->path, pos, ms_tag_str(sf->root.id));
	if (ms_u32be(&sf->root) != MS_TAG('I', 'M', 'T', 'S'))
		ms_fatal("%s: RIFF @0x%zx is not IMTS", f->path, pos);
	MsChunk rate = raw_chunk(f, sf->root.pos, sf->root.end);
	if (rate.id != MS_TAG('r', 'a', 't', 'e'))
		ms_fatal("%s: expected rate chunk @0x%zx", f->path, sf->root.pos);
	sf->rate = ms_u32(&rate);
	size_t p = rate.end;
	if (p & 1)
		p++;
	MsChunk list = raw_chunk(f, p, sf->root.end);
	if (list.id != MS_TAG('L', 'I', 'S', 'T'))
		ms_fatal("%s: expected LIST @0x%zx, got %s", f->path, p, ms_tag_str(list.id));
	if (ms_u32be(&list) != MS_TAG('d', 'a', 't', 'a'))
		ms_fatal("%s: LIST @0x%zx is not data", f->path, p);
	sf->first_chunk = list.pos;
	/* the chunks are bounded by the RIFF chunk (the LIST length is not always reliable) */
	return sf->root.end;
}

int ms_subfile_next_chunk(const MsSubfile *sf, size_t *pos, MsChunk *out) {
	size_t p = *pos;
	if (p & 1)
		p++;
	if (p + 8 > sf->root.end)
		return 0;
	MsFile tmp = {{0}, (uint8_t *)sf->root.base, sf->root.end};
	*out = raw_chunk(&tmp, p, sf->root.end);
	*pos = out->end;
	return 1;
}

/* ---- typed datums ---- */

uint16_t ms_peek_type(const MsChunk *c) {
	if (c->pos + 2 > c->end)
		return 0xffff;
	return (uint16_t)(c->base[c->pos] | c->base[c->pos + 1] << 8);
}

static void expect(MsChunk *c, uint16_t type) {
	size_t at = c->pos;
	uint16_t t = ms_u16(c);
	if (t != type)
		ms_fatal("expected datum type 0x%x, got 0x%x (@0x%zx in %s)", type, t, at, ms_tag_str(c->id));
}

uint8_t ms_t_u8(MsChunk *c) {
	expect(c, DATUM_UINT8);
	return ms_u8(c);
}

uint16_t ms_t_u16(MsChunk *c) {
	expect(c, DATUM_UINT16);
	return ms_u16(c);
}

uint32_t ms_t_u32(MsChunk *c) {
	expect(c, DATUM_UINT32);
	return ms_u32(c);
}

int8_t ms_t_s8(MsChunk *c) {
	expect(c, DATUM_INT8);
	return (int8_t)ms_u8(c);
}

int16_t ms_t_s16(MsChunk *c) {
	expect(c, DATUM_INT16);
	return ms_s16(c);
}

int32_t ms_t_s32(MsChunk *c) {
	expect(c, DATUM_INT32);
	return (int32_t)ms_u32(c);
}

float ms_t_float(MsChunk *c) {
	expect(c, DATUM_FLOAT);
	uint32_t u = ms_u32(c);
	float v;
	memcpy(&v, &u, 4);
	return v;
}

static double raw_double(MsChunk *c) {
	uint64_t lo = ms_u32(c), hi = ms_u32(c);
	uint64_t u = lo | hi << 32;
	double v;
	memcpy(&v, &u, 8);
	return v;
}

double ms_t_double(MsChunk *c) {
	expect(c, DATUM_DOUBLE);
	return raw_double(c);
}

double ms_t_time(MsChunk *c) {
	expect(c, DATUM_TIME);
	return raw_double(c);
}

int16_t ms_t_gunit(MsChunk *c) {
	expect(c, DATUM_GRAPHIC_UNIT);
	return ms_s16(c);
}

MsPoint ms_t_point(MsChunk *c) {
	expect(c, DATUM_POINT);
	MsPoint p;
	p.x = ms_t_gunit(c);
	p.y = ms_t_gunit(c);
	return p;
}

MsPoint ms_t_gsize(MsChunk *c) {
	expect(c, DATUM_GRAPHIC_SIZE);
	MsPoint p;
	p.x = ms_t_gunit(c);
	p.y = ms_t_gunit(c);
	return p;
}

MsRect ms_t_rect(MsChunk *c) {
	expect(c, DATUM_RECT);
	MsPoint o = ms_t_point(c), s = ms_t_gsize(c);
	MsRect r = {o.x, o.y, s.x, s.y};
	return r;
}

uint32_t ms_t_channel(MsChunk *c) {
	expect(c, DATUM_CHANNEL_IDENT);
	return ms_u32be(c);
}

static char *read_cstr(MsChunk *c) {
	uint32_t n = ms_t_u32(c);
	need(c, n);
	char *s = malloc((size_t)n + 1);
	memcpy(s, c->base + c->pos, n);
	s[n] = 0;
	c->pos += n;
	return s;
}

char *ms_t_string(MsChunk *c) {
	expect(c, DATUM_STRING);
	return read_cstr(c);
}

char *ms_t_filename(MsChunk *c) {
	expect(c, DATUM_FILENAME);
	return read_cstr(c);
}

void ms_t_version(MsChunk *c, uint16_t *major, uint16_t *minor, uint16_t *patch) {
	expect(c, DATUM_VERSION);
	*major = ms_t_u16(c);
	*minor = ms_t_u16(c);
	*patch = ms_t_u16(c);
}
