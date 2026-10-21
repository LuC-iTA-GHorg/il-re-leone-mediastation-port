/* 8-bit palettized bitmaps of images, sprite frames and movie frames.
 * Header (ImageInfo): data offset from the chunk start, size, compression, stride.
 * Compression: 0 raw, 1 RLE8, 5/6 "CCC" (later engines only), 7 raw with colour 0 transparent.
 * RLE8 bitmaps are kept compressed and decoded when drawn (as in the original), unless the
 * actor asks for "decompress in place" (load type). See docs/DATA_FORMAT.md. */
#ifndef MS_BITMAP_H
#define MS_BITMAP_H

#include <stdint.h>

#include "datafile.h"

enum {
	BMP_RAW = 0,
	BMP_RLE8 = 1,
	BMP_CCC = 5,
	BMP_CCC_TRANSPARENT = 6,
	BMP_RAW_TRANSPARENT = 7
};

typedef struct {
	int w, h, stride;
	int compression;
	uint8_t *pixels; /* stride*h, when not compressed */
	uint8_t *data;   /* compressed data */
	uint32_t data_size;
	uint8_t *decoded; /* w*h decoded copy, kept for movie keyframes */
} MsBitmap;

typedef struct {
	uint16_t data_offset;
	int16_t w, h;
	uint16_t compression;
	int16_t stride;
} MsImageInfo;

MsImageInfo bitmap_read_info(MsChunk *c);
/* Reads the pixel data that follows the header (seeks to chunk start + data_offset). */
MsBitmap *bitmap_read(MsChunk *c, const MsImageInfo *info, int decompress_in_place);
void bitmap_free(MsBitmap *b);
int bitmap_is_compressed(const MsBitmap *b);

/* Draws an RLE8 bitmap at (x, y) into dst (pitch bytes per line), clipped to
 * [left, right) x [top, bottom), exactly as the original blitter (see bitmap.c). */
void bitmap_rle8_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y, int left,
	int top, int right, int bottom);
/* CCC compression 5 (opaque) and 6 (transparent), see bitmap.c. */
void bitmap_ccc_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y, int left,
	int top, int right, int bottom);
void bitmap_ccc_transparent_blit(const MsBitmap *b, uint8_t *dst, int pitch, int x, int y,
	int left, int top, int right, int bottom);
/* Decodes an RLE8 bitmap into dst (w*h, pitch dst_pitch). Pixels not written by the RLE
 * stream (transparent runs/skips) are left untouched in dst. Returns 0 on bad data. */
int bitmap_rle8_decode(const MsBitmap *b, uint8_t *dst, int dst_pitch);
/* Delta frames of movies: the "copy from keyframe" operations read the keyframe pixels
 * (key_w*key_h, already decoded), placed at (key_dx, key_dy) relative to the delta frame. */
int bitmap_rle8_decode_delta(const MsBitmap *b, uint8_t *dst, int dst_pitch, const uint8_t *key,
	int key_w, int key_h, int key_dx, int key_dy);

#endif
