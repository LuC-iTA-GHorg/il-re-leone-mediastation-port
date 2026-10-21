/* Services of the runtime used by the actors: clock, event queue, alarms, screen
 * invalidation, mouse, cursors, audio, script execution. Implemented by runtime.c (the
 * SDL2 player); tools that only load the data (msdump) link runtime_stub.c instead. */
#ifndef MS_RUNTIME_H
#define MS_RUNTIME_H

#include <stdint.h>

#include "actor.h"
#include "datafile.h"
#include "value.h"

/* Milliseconds since the start of the title (getTotalPlayTime in the original). */
uint32_t rt_now(void);
/* random numbers for the scripts and the minigames */
uint32_t rt_random(void);

/* Event queue (EventLoop): delivered to actor->vt->on_event in order. */
void rt_queue_actor_event(uint16_t actor_id, int event, const MsValue *arg);
void rt_queue_branch(uint16_t screen_id, int disable_auto_update);
void rt_queue_enable_auto_update(uint32_t token);

/* Display */
void rt_invalidate(MsRect r);
uint32_t rt_disable_auto_update(void);
void rt_enable_auto_update(uint32_t token);
void rt_draw_bitmap(const MsBitmap *b, int x, int y, double dissolve);
void rt_draw_delta(const MsBitmap *delta, const MsBitmap *key, int x, int y, int key_dx, int key_dy);
void rt_draw_thick_line(int x0, int y0, int x1, int y1, int thickness, uint8_t colour);
/* palette index closest to an RGB colour, in the current palette */
uint8_t rt_find_color(uint8_t r, uint8_t g, uint8_t b);
/* redraws the dirty rectangles and shows the screen now (used inside scripts) */
void rt_perform_update_dirty(void);

/* Mouse */
void rt_mouse_pos(int *x, int *y);
void rt_invalidate_mouse(void);
MsActor *rt_mouse_inside_hotspot(void);
MsActor *rt_mouse_down_hotspot(void);
void rt_set_mouse_inside_hotspot(MsActor *a);
void rt_set_mouse_down_hotspot(MsActor *a);

/* Cursors */
void rt_cursor_register_permanent(uint16_t id);
void rt_cursor_set_permanent(uint16_t id);
void rt_cursor_set_temporary(uint16_t id);
void rt_cursor_unset_temporary(void);

/* Pre-display sync clients (playing movies update their frames before each redraw). */
void rt_register_sync(MsActor *a);
/* answers of movie_pre_display_sync (service 1190:fbc4 of the original, whose own values are
 * 0 changed, 1 idle, 2 skip the screen update of this pass) */
enum { RT_SYNC_IDLE = 0, RT_SYNC_CHANGED = 1, RT_SYNC_SKIP = 2 };
void rt_unregister_sync(MsActor *a);

/* Scripts */
MsValue rt_run_code(uint16_t actor_id, const uint8_t *code, uint32_t size, MsValue *args, int nargs);
MsValue rt_call_function(uint32_t function_id, MsValue *args, int nargs);

/* Document */
void rt_quit(void);
int rt_context_load_in_progress(uint16_t context_id);
void rt_set_multiple_sounds(int allow);
void rt_set_multiple_streams(int allow);

/* Audio: a sequence of PCM/ADPCM chunks played back to back. */
typedef struct MsAudioSeq MsAudioSeq;
struct MsAudioSeq {
	uint16_t chunk_count;
	uint32_t rate;
	uint16_t channels, bits;
	uint8_t **chunks;
	uint32_t *sizes;
	int nchunks;
	int voice;    /* mixer voice, -1 when idle */
	int state;    /* AUDIO_STOPPED, AUDIO_PLAYING, AUDIO_PAUSED */
	void (*on_state)(MsAudioSeq *s, int state, int reason, void *user);
	void *user;
	uint32_t played; /* source samples played by the mixer since the start (audio.c) */
};
enum { AUDIO_STOPPED = 1, AUDIO_PLAYING = 2, AUDIO_PAUSED = 3 };
enum { STOP_NONE = 0, STOP_FAILURE = 1, STOP_END = 2, STOP_SCRIPT = 3, STOP_ABORT = 4 };

void audio_seq_read_params(MsAudioSeq *s, MsChunk *c);
/* PCM formats the original wave player accepts (audio_data.c, LIONKING.EXE 1188:27ae) */
int audio_format_supported(uint32_t rate, uint16_t channels, uint16_t bits);
/* one sample of a PCM chunk (8-bit unsigned / 16-bit LE, interleaved channels) */
int16_t audio_pcm_sample(const MsAudioSeq *s, int chunk, uint32_t frame, int channel, int *ok);
void audio_seq_add_chunk(MsAudioSeq *s, MsChunk *c);
void audio_seq_free(MsAudioSeq *s);
void audio_seq_start(MsAudioSeq *s);
void audio_seq_stop(MsAudioSeq *s);
void audio_seq_pause(MsAudioSeq *s);
void audio_seq_resume(MsAudioSeq *s);
/* Playback position in ms: samples actually played by the mixer (frozen while the sequence or
 * the device is paused; the last position after the end). In the original a playing movie
 * takes its time from its sound (wave buffer positions, IMTWIN.DLL WAVEPLAYCB). */
uint32_t audio_seq_position_ms(const MsAudioSeq *s);
/* forgets a sequence that is being freed (audio.c keeps the list of playing ones) */
void audio_untrack(MsAudioSeq *s);

/* Device ownership: with exclusive sounds, starting a sound stops the previous one. */
int rt_grant_sound_device(MsActor *a);
void rt_release_sound_device(MsActor *a);

/* Root stage (runtime.c) */
void rt_stage_remove(MsActor *a);
void rt_stage_resort(void);

/* Player (runtime.c) */
void rt_init(void);
void rt_run(const char *data_dir, uint16_t start_screen);
void rt_add_scripted_click(uint32_t at, int x, int y);
void rt_add_scripted_key(uint32_t at, int code);
void rt_add_scripted_shot(uint32_t at, const char *path);
/* type: 0 button down, 1 button up, 2 move only */
void rt_add_scripted_mouse(uint32_t at, int type, int x, int y);
void rt_set_quit_after(uint32_t ms);
void rt_set_seed(uint32_t seed);
/* --trace line for an event delivered to an actor (also for responses run directly) */
void rt_trace_event(MsActor *a, int ev, const MsValue *arg);

#endif
