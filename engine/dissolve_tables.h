/* Partial-dissolve patterns of the original player, read at start-up from LIONKING.EXE
 * (the tables are Disney data: they are not copied into this repository).
 *
 * In the original (see docs/ORIGINAL.md) the automatic data segment (DGROUP) holds 12
 * tables of 25x25 bytes (values 0/1, 626 bytes apart), one 24x24 checkerboard, and at
 * DS:2358 an array of 25 descriptors of 8 bytes:
 *     u16 size (25 or 24), u16 threshold (0 or 1), u16 offset, u16 segment (relocated)
 * Descriptors 0..11 use the tables with threshold 1, 12..23 the same tables in reverse
 * order with threshold 0 (the complement), 24 is the 50% checkerboard. A pixel
 * is drawn when table[row * size + col] == threshold; how row and col advance depends on
 * the blit (see video.c and docs/ORIGINAL.md).
 * The same data is in ScummVM's dissolvepatterns.h (checked byte for byte).
 *
 * Included only by video.c. */
#ifndef DISSOLVE_TABLES_H
#define DISSOLVE_TABLES_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DISSOLVE_COUNT 25
#define DISSOLVE_DESC_OFFSET 0x2358 /* in DGROUP of LIONKING.EXE (08/09/1995, 363200 bytes) */

typedef struct {
	int size;
	uint8_t threshold;
	/* one byte more than size*size: the RLE blit of the original reads column "size"
	 * (an off-by-one), which on the last row is the byte after the table */
	uint8_t data[25 * 25 + 1];
} DissolvePattern;

static DissolvePattern dissolve_patterns[DISSOLVE_COUNT];
static int dissolve_loaded;

static unsigned dt_rd16(const uint8_t *p) {
	return p[0] | p[1] << 8;
}

/* Reads the 25 patterns from the NE executable. Returns 1 on success; on any mismatch
 * nothing is kept and the caller falls back to an approximation. */
static int dissolve_tables_load(const char *exe_path) {
	FILE *f = fopen(exe_path, "rb");
	if (!f)
		return 0;
	uint8_t *d = NULL;
	long size = 0;
	if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0x40 && fseek(f, 0, SEEK_SET) == 0) {
		d = malloc((size_t)size);
		if (d && fread(d, 1, (size_t)size, f) != (size_t)size) {
			free(d);
			d = NULL;
		}
	}
	fclose(f);
	if (!d)
		return 0;
	int ok = 0;
	uint32_t ne = d[0x3c] | d[0x3d] << 8 | (uint32_t)d[0x3e] << 16 | (uint32_t)d[0x3f] << 24;
	if (ne + 0x40 <= (uint32_t)size && d[ne] == 'N' && d[ne + 1] == 'E') {
		unsigned autods = dt_rd16(d + ne + 0x0e);    /* 1-based segment number */
		unsigned nseg = dt_rd16(d + ne + 0x1c);
		uint32_t segtab = ne + dt_rd16(d + ne + 0x22);
		unsigned shift = dt_rd16(d + ne + 0x32);
		if (autods >= 1 && autods <= nseg && segtab + nseg * 8 <= (uint32_t)size) {
			const uint8_t *e = d + segtab + (autods - 1) * 8;
			uint32_t seg = (uint32_t)dt_rd16(e) << shift;
			uint32_t len = dt_rd16(e + 2) ? dt_rd16(e + 2) : 0x10000;
			if (seg + len <= (uint32_t)size && DISSOLVE_DESC_OFFSET + DISSOLVE_COUNT * 8 <= len) {
				const uint8_t *ds = d + seg;
				ok = 1;
				for (int i = 0; i < DISSOLVE_COUNT && ok; i++) {
					const uint8_t *desc = ds + DISSOLVE_DESC_OFFSET + i * 8;
					unsigned sz = dt_rd16(desc), th = dt_rd16(desc + 2), off = dt_rd16(desc + 4);
					if (sz < 2 || sz > 25 || th > 1 || off + sz * sz + 1 > len) {
						ok = 0;
						break;
					}
					dissolve_patterns[i].size = (int)sz;
					dissolve_patterns[i].threshold = (uint8_t)th;
					for (unsigned k = 0; k <= sz * sz; k++) {
						if (k < sz * sz && ds[off + k] > 1)
							ok = 0;
						dissolve_patterns[i].data[k] = ds[off + k];
					}
				}
			}
		}
	}
	free(d);
	dissolve_loaded = ok;
	return ok;
}

/* Integral percentage of a dissolve factor: (int)(f * 100.0 + 0.5), truncated with _ftol
 * (LIONKING.EXE 10a0:0035, constants 100.0 and 0.5 at DS:2FC6). */
static int dissolve_percent(double f) {
	return (int)(f * 100.0 + 0.5);
}

/* Pattern for a percentage in 1..99 (LIONKING.EXE 10b8:0314): 50 uses the checkerboard
 * (24), otherwise (p + 2) / 4 clamped to 1..24, minus one. ScummVM omits the "minus
 * one" and so picks the next denser pattern (e.g. 25 % -> 28 % of the pixels instead of 24 %). */
static int dissolve_index(int p) {
	if (p == 50)
		return DISSOLVE_COUNT - 1;
	int i = (p + 2) / 4;
	i = i < 1 ? 1 : i > DISSOLVE_COUNT - 1 ? DISSOLVE_COUNT - 1 : i;
	return i - 1;
}

#endif
