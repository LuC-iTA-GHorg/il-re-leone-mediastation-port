/* Unit test of the CCC decoders (engine\bitmap.c, compressions 5 and 6 of LIONKING.EXE
 * 10b8:1840 and 10b8:19f6). No game uses them: the data here is encoded by hand.
 * Build and run: tools\ccctest\ccctest.bat */
#include <stdio.h>
#include <string.h>

#include "bitmap.h"

static int failures;

static void expect(const char *what, const uint8_t *got, const char *want, int w, int h, int pitch) {
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			char c = want[y * w + x];
			uint8_t v = c == '.' ? 0xee : (uint8_t)(c - '0');
			if (got[y * pitch + x] != v) {
				printf("FAIL %s at %d,%d: got %d, want %d\n", what, x, y, got[y * pitch + x], v);
				failures++;
				return;
			}
		}
	printf("ok   %s\n", what);
}

int main(void) {
	uint8_t fb[8 * 8];

	/* type 5: 6x5 image = 2x2 cells, cut at the right (2 columns) and bottom (1 row) */
	uint8_t ccc5[] = {
		1, 2, 0x84, 0x21, /* cell (0,0): mask 1000 0100 0010 0001 = diagonal of colour 2 */
		3, 4, 0xf0, 0x00, /* cell (4,0): first row colour 4; only columns 0-1 are used */
		5, 6, 0x0f, 0xff, /* cell (0,4): only row 0 is used, all colour 5 */
		7, 8, 0x80, 0x00, /* cell (4,4): pixel (0,0) colour 8, (1,0) colour 7 */
	};
	MsBitmap b5 = {6, 5, 8, BMP_CCC, NULL, ccc5, sizeof(ccc5), NULL};
	memset(fb, 0xee, sizeof(fb));
	bitmap_ccc_blit(&b5, fb, 8, 1, 1, 0, 0, 8, 8);
	expect("ccc5 full", fb,
		"........"
		".211144."
		".121133."
		".112133."
		".111233."
		".555587."
		"........"
		"........",
		8, 8, 8);
	memset(fb, 0xee, sizeof(fb));
	bitmap_ccc_blit(&b5, fb, 8, 1, 1, 2, 2, 5, 4);
	expect("ccc5 clipped", fb,
		"........"
		"........"
		"..211..."
		"..121..."
		"........"
		"........"
		"........"
		"........",
		8, 8, 8);

	/* type 6: 8x8 image. Key colour 9; row 0: one cell at column 4; row 4 (down 1 cell, column
	 * 0): two cells, the first with colour 0 = key (only its 1-bits are drawn). */
	uint8_t ccc6[] = {
		0xfe, 9, 0, 0,    /* key = 9 */
		0, 1, 0, 1,       /* 1 cell, same row, column += 1 cell */
		1, 2, 0xff, 0xff, /* all colour 2 */
		0, 2, 1, 0,       /* 2 cells, row += 1 cell, column = 0 */
		9, 3, 0x80, 0x01, /* transparent except (0,0) and (3,3) */
		4, 5, 0x00, 0x00, /* all colour 4 */
		0xff, 0, 0, 0,    /* end */
		0, 1, 0, 0,       /* never read */
		6, 6, 0xff, 0xff,
	};
	MsBitmap b6 = {8, 8, 8, BMP_CCC_TRANSPARENT, NULL, ccc6, sizeof(ccc6), NULL};
	memset(fb, 0xee, sizeof(fb));
	bitmap_ccc_transparent_blit(&b6, fb, 8, 0, 0, 0, 0, 8, 8);
	expect("ccc6 full", fb,
		"....2222"
		"....2222"
		"....2222"
		"....2222"
		"3...4444"
		"....4444"
		"....4444"
		"...34444",
		8, 8, 8);
	memset(fb, 0xee, sizeof(fb));
	bitmap_ccc_transparent_blit(&b6, fb, 8, 0, 0, 3, 3, 6, 5);
	expect("ccc6 clipped", fb,
		"........"
		"........"
		"........"
		"....22.."
		"....44.."
		"........"
		"........"
		"........",
		8, 8, 8);
	memset(fb, 0xee, sizeof(fb));
	bitmap_ccc_transparent_blit(&b6, fb, 8, 0, 0, 0, 0, 8, 4);
	expect("ccc6 stops at the clip bottom", fb,
		"....2222"
		"....2222"
		"....2222"
		"....2222"
		"........"
		"........"
		"........"
		"........",
		8, 8, 8);

	printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}
