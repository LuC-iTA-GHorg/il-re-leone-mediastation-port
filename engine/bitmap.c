#include "bitmap.h"

#include <stdlib.h>
#include <string.h>

MsImageInfo bitmap_read_info(MsChunk *c) {
	MsImageInfo i;
	i.data_offset = ms_t_u16(c);
	MsPoint size = ms_t_gsize(c);
	i.w = size.x;
	i.h = size.y;
	i.compression = ms_t_u16(c);
	i.stride = (int16_t)ms_t_u16(c);
	return i;
}

/* Only RLE8 (1) and CCC (5, 6) are kept compressed. The blit dispatcher of LIONKING.EXE
 * (10a0:0000) draws every other compression value (0, 2-4, 8...) as an opaque uncompressed
 * DIB and only 7 with colour 0 transparent. */
int bitmap_is_compressed(const MsBitmap *b) {
	return b->compression == BMP_RLE8 || b->compression == BMP_CCC ||
		b->compression == BMP_CCC_TRANSPARENT;
}

/* Uncompressed pixels are a top-down DIB: the original builds a BITMAPINFOHEADER with the
 * image width and gets the row length as ((w * 8 + 31) & ~31) / 8 (LIONKING.EXE 10e8:0000,
 * 10e8:09ee); the stride field of the header is not used. In the Lion King data every
 * uncompressed image has stride == width == a multiple of 4. */
static int dib_stride(int w) {
	return (w + 3) & ~3;
}

MsBitmap *bitmap_read(MsChunk *c, const MsImageInfo *info, int decompress_in_place) {
	MsBitmap *b = calloc(1, sizeof(MsBitmap));
	b->w = info->w;
	b->h = info->h;
	b->stride = dib_stride(info->w);
	b->compression = info->compression;
	c->pos = c->start + info->data_offset;
	size_t n = ms_remaining(c);
	if (n == 0)
		return b;
	if (bitmap_is_compressed(b)) {
		b->data = malloc(n);
		b->data_size = (uint32_t)n;
		ms_read(c, b->data, n);
		if (decompress_in_place && b->compression == BMP_RLE8) {
			/* LIONKING.EXE 1070:00c6: a buffer of DIB stride * h bytes filled with 0
			 * (10e0:0000), the RLE drawn into it with the display blitter (1188:425e), then
			 * the bitmap becomes compression 0: opaque, so skipped areas show colour 0. */
			b->pixels = calloc((size_t)b->stride * b->h, 1);
			bitmap_rle8_blit(b, b->pixels, b->stride, 0, 0, 0, 0, b->w, b->h);
			free(b->data);
			b->data = NULL;
			b->data_size = 0;
			b->compression = BMP_RAW;
		}
	} else {
		size_t need = (size_t)b->stride * b->h;
		b->pixels = calloc(need, 1);
		ms_read(c, b->pixels, need < n ? need : n);
		c->pos = c->end;
	}
	return b;
}

void bitmap_free(MsBitmap *b) {
	if (!b)
		return;
	free(b->pixels);
	free(b->data);
	free(b->decoded);
	free(b);
}

/* RLE8 as decoded by the original player (the ScummVM reimplementation calls it
 * decompressRle8Bitmap):
 *   n>0, c       -> n pixels of colour c
 *   0,0          -> end of line
 *   0,1          -> end of image
 *   0,2,dx,dy    -> copy dx pixels (and dy more lines) from the keyframe (delta frames)
 *   0,3,dx,dy    -> skip (transparent)
 *   0,n>=4,...   -> n literal pixels, then padding to an even offset */
/* The RLE8 blitter of the original (LIONKING.EXE 1188:40be / 1188:425e -> 10e8:09ee ->
 * 1118:0024), straight from the compressed data into dst (pitch bytes per line), the image
 * placed at (x, y) and clipped to [left, right) x [top, bottom). Runs and literal pixels are
 * copied as they are, colour 0 included: only skips (0,3), keyframe copies (0,2) and the rest
 * of a line after an end of line leave dst untouched. A run never continues on the next line. */
void bitmap_rle8_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y, int left,
	int top, int right, int bottom) {
	const uint8_t *p = b->data, *end = b->data + b->data_size;
	int cx = x, cy = y;
	if (!p)
		return;
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
		if (first >= last)
			continue;
		uint8_t *d = dst + (size_t)cy * pitch + run_x;
		if (literal)
			memcpy(d + first, src + first, (size_t)(last - first));
		else
			memset(d + first, arg, (size_t)(last - first));
	}
}

/* CCC ("colour cell compression"), not used by the Lion King data. The image is made of 4x4
 * cells of 4 bytes: colour 0, colour 1, then a 16-bit mask, high byte first; the pixel at row
 * r, column c of the cell takes colour 1 if bit 15 - (4r + c) is set, else colour 0. Cells
 * at the right and bottom edges are cut to the image. */
static void ccc_cell(uint8_t *dst, int pitch, int x, int y, const uint8_t *cell, int rows,
	int cols, int left, int top, int right, int bottom, int key_skip) {
	uint8_t c0 = cell[0], c1 = cell[1];
	unsigned mask = (unsigned)cell[2] << 8 | cell[3];
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			int px = x + c, py = y + r;
			if (px < left || px >= right || py < top || py >= bottom)
				continue;
			int one = (mask >> (15 - (4 * r + c))) & 1;
			if (!one && key_skip)
				continue;
			dst[(size_t)py * pitch + px] = one ? c1 : c0;
		}
}

/* Type 5 (LIONKING.EXE 10b8:1840): the cells follow each other in raster order, rows of cells
 * from the top, every cell present. The original draws the whole image in one go, without
 * the dirty rectangles; here it is clipped to [left, right) x [top, bottom) (the screen). */
void bitmap_ccc_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y, int left,
	int top, int right, int bottom) {
	const uint8_t *p = b->data, *end = b->data + b->data_size;
	for (int cy = 0; cy < b->h; cy += 4) {
		int rows = b->h - cy < 4 ? b->h - cy : 4;
		for (int cx = 0; cx < b->w; cx += 4) {
			if (p + 4 > end)
				return;
			int cols = b->w - cx < 4 ? b->w - cx : 4;
			ccc_cell(dst, pitch, x + cx, y + cy, p, rows, cols, left, top, right, bottom, 0);
			p += 4;
		}
	}
}

/* Type 6, transparent (LIONKING.EXE 10b8:010e -> 10b8:19f6, once per dirty rectangle): a
 * sequence of 4-byte records. 0xff (as a 32-bit word: ff 00 00 00) ends the image; a record
 * starting with 0xfe sets the transparent key colour to its second byte (the key starts as
 * 0); any other record is a run header: n = byte0 * 256 + byte1 cells follow, and if byte2
 * is not 0 the cell row moves down by byte2 cells and the cell column becomes byte3,
 * otherwise the column moves right by byte3 cells. The n cells are consecutive to the right.
 * A cell whose colour 0 equals the key draws only its colour-1 pixels. Drawing stops at the
 * first run whose row is at or below the bottom of the clip rectangle. */
void bitmap_ccc_transparent_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y,
	int left, int top, int right, int bottom) {
	const uint8_t *p = b->data, *end = b->data + b->data_size;
	int row = 0, col = 0; /* in pixels, multiples of 4 */
	uint8_t key = 0;
	int clip_bottom = bottom - y; /* image coordinates */
	if (clip_bottom > b->h)
		clip_bottom = b->h;
	while (p + 4 <= end) {
		uint32_t w = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
		if (w == 0xff)
			return;
		if (p[0] == 0xfe) {
			key = p[1];
			p += 4;
			continue;
		}
		unsigned n = (unsigned)p[0] << 8 | p[1];
		if (p[2]) {
			row += p[2] * 4;
			col = p[3] * 4;
		} else {
			col += p[3] * 4;
		}
		p += 4;
		if (row >= clip_bottom)
			return;
		for (unsigned i = 0; i < n && p + 4 <= end; i++, p += 4, col += 4) {
			int rows = b->h - row < 4 ? b->h - row : 4;
			int cols = b->w - col < 4 ? b->w - col : 4;
			if (cols <= 0 || rows <= 0)
				continue;
			int r0 = top > y + row ? top : y + row, r1 = bottom < y + b->h ? bottom : y + b->h;
			int c0 = left > x ? left : x, c1 = right < x + b->w ? right : x + b->w;
			ccc_cell(dst, pitch, x + col, y + row, p, rows, cols, c0, r0, c1, r1, p[0] == key);
		}
	}
}

int bitmap_rle8_decode(const MsBitmap *b, uint8_t *dst, int dst_pitch) {
	return bitmap_rle8_decode_delta(b, dst, dst_pitch, NULL, 0, 0, 0, 0);
}

int bitmap_rle8_decode_delta(const MsBitmap *b, uint8_t *dst, int dst_pitch, const uint8_t *key,
	int key_w, int key_h, int key_dx, int key_dy) {
	const uint8_t *p = b->data, *end = b->data + b->data_size;
	int w = b->w, h = b->h;
	int x = 0, y = 0;
	long total = (long)w * h;
#define NEXT() (p < end ? *p++ : 0)
	while (y < h) {
		x = 0;
		int done = 0;
		for (;;) {
			if (y >= h || p >= end) {
				done = 1;
				break;
			}
			uint8_t op = NEXT();
			if (op == 0) {
				op = NEXT();
				if (op == 0) {
					if (p >= end)
						done = 1;
					break;
				} else if (op == 1) {
					done = 1;
					break;
				} else if (op == 2) {
					int cx = NEXT(), cy = NEXT();
					if (key) {
						for (int line = 0; line <= cy; line++) {
							/* lines y..y+cy-1 first, then the current line (same result) */
							int ky = y + line - key_dy, kx = x - key_dx;
							int dy = y + line;
							if (line == cy) {
								ky = y - key_dy;
								dy = y;
							}
							if (kx >= 0 && kx + cx <= key_w && ky >= 0 && ky < key_h && dy < h &&
								x + cx <= w)
								memcpy(dst + (size_t)dy * dst_pitch + x, key + (size_t)ky * key_w + kx,
									(size_t)cx);
						}
					}
					x += cx;
					y += cy;
				} else if (op == 3) {
					x += NEXT();
					y += NEXT();
				} else {
					int n = op;
					long room = total - ((long)y * w + x);
					if (n > room)
						n = (int)(room < 0 ? 0 : room);
					/* a literal run may continue on the next lines of the image */
					for (int i = 0; i < n; i++) {
						uint8_t px = NEXT();
						long off = (long)y * w + x + i;
						dst[(off / w) * dst_pitch + off % w] = px;
					}
					for (int i = n; i < op; i++)
						(void)NEXT();
					if ((p - b->data) & 1)
						(void)NEXT();
					x += n;
				}
			} else {
				uint8_t colour = NEXT();
				int n = op;
				long room = total - ((long)y * w + x);
				if (n > room)
					n = (int)(room < 0 ? 0 : room);
				for (int i = 0; i < n; i++) {
					long off = (long)y * w + x + i;
					dst[(off / w) * dst_pitch + off % w] = colour;
				}
				x += n;
			}
		}
		y++;
		if (done)
			break;
	}
#undef NEXT
	return 1;
}
