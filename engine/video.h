/* Screen: a 640x480 8-bit frame buffer with a 256-colour palette, shown through SDL2. */
#ifndef MS_VIDEO_H
#define MS_VIDEO_H

#include <stdint.h>

#include "datafile.h"
#include "value.h"

#define SCREEN_W 640
#define SCREEN_H 480

int video_init(int fullscreen, int scale);
void video_shutdown(void);
void video_toggle_fullscreen(void);
/* window coordinates -> game coordinates */
void video_window_to_game(int wx, int wy, int *gx, int *gy);

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
