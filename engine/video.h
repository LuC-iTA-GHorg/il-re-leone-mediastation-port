/* Screen: a 640x480 8-bit frame buffer with a 256-colour palette, shown through SDL2. */
#ifndef MS_VIDEO_H
#define MS_VIDEO_H

#include <stdint.h>

#include "datafile.h"
#include "value.h"

#define SCREEN_W 640
#define SCREEN_H 480

/* Port options (lionking.ini, command line). The game is always drawn at 640x480; these only
 * change how the frame is shown and how palette fades run. */
enum { VIDEO_ASPECT_4_3, VIDEO_ASPECT_STRETCH, VIDEO_ASPECT_INTEGER };
typedef struct {
	int fullscreen;
	int scale;         /* window size: SCREEN_W * scale x SCREEN_H * scale */
	int aspect;        /* VIDEO_ASPECT_* */
	int smooth;        /* linear filtering instead of nearest pixel */
	int instant_fades; /* palette fades jump to the final palette (exe on a 16/24-bit screen) */
} VideoOptions;

int video_init(const VideoOptions *opt);
void video_shutdown(void);
void video_toggle_fullscreen(void);
/* window coordinates -> game coordinates, clamped to the screen */
void video_window_to_game(int wx, int wy, int *gx, int *gy);
/* Mouse event coordinates -> game coordinates, not clamped (out of the screen while captured).
 * With a logical size SDL already delivers them in game pixels; stretched, they are window
 * pixels. */
void video_event_to_game(int *x, int *y);

/* Drawing is clipped to this rectangle (one rectangle of the dirty region at a time). */
void video_set_clip(MsRect r);
/* The rectangles of the clip region of a redraw: every blit walks them in order (NULL: only
 * the rectangle of video_set_clip). The array must stay valid until it is reset. */
void video_set_clip_list(const MsRect *r, int n);

/* Shows the frame buffer with the current palette. */
void video_present(void);
/* Runs the transition scheduled with EffectTransitionOnSync, if any (after a flush). */
void video_do_transition_on_sync(void);

void video_set_palette(const uint8_t *pal768, int start, int count);
const uint8_t *video_palette(void);

/* Reads the 25 dissolve patterns of the original from LIONKING.EXE (1 = found). */
int video_load_dissolve_tables(const char *exe_path);

/* Writes the frame buffer as a 24-bit BMP (screenshots for the tests). */
int video_save_bmp(const char *path);

#endif
