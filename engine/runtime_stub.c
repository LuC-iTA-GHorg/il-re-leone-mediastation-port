/* Runtime without display, audio or scripts: lets tools (msdump) load the data. */
#include "runtime.h"

uint32_t rt_now(void) {
	return 0;
}
void rt_trace_event(MsActor *a, int ev, const MsValue *arg) {
	(void)a;
	(void)ev;
	(void)arg;
}

void rt_queue_actor_event(uint16_t actor_id, int event, const MsValue *arg) {
	(void)actor_id;
	(void)event;
	(void)arg;
}
void rt_queue_branch(uint16_t screen_id, int disable_auto_update) {
	(void)screen_id;
	(void)disable_auto_update;
}
void rt_queue_enable_auto_update(uint32_t token) {
	(void)token;
}
void rt_invalidate(MsRect r) {
	(void)r;
}
uint32_t rt_disable_auto_update(void) {
	return 0;
}
void rt_enable_auto_update(uint32_t token) {
	(void)token;
}
void rt_draw_bitmap(const MsBitmap *b, int x, int y, double dissolve) {
	(void)b;
	(void)x;
	(void)y;
	(void)dissolve;
}
void rt_draw_delta(const MsBitmap *delta, const MsBitmap *key, int x, int y, int key_dx, int key_dy) {
	(void)delta;
	(void)key;
	(void)x;
	(void)y;
	(void)key_dx;
	(void)key_dy;
}
void rt_mouse_pos(int *x, int *y) {
	*x = *y = 0;
}
void rt_invalidate_mouse(void) {
}
MsActor *rt_mouse_inside_hotspot(void) {
	return NULL;
}
MsActor *rt_mouse_down_hotspot(void) {
	return NULL;
}
void rt_set_mouse_inside_hotspot(MsActor *a) {
	(void)a;
}
void rt_set_mouse_down_hotspot(MsActor *a) {
	(void)a;
}
void rt_cursor_register_permanent(uint16_t id) {
	(void)id;
}
void rt_cursor_set_permanent(uint16_t id) {
	(void)id;
}
void rt_cursor_set_temporary(uint16_t id) {
	(void)id;
}
void rt_cursor_unset_temporary(void) {
}
void rt_register_sync(MsActor *a) {
	(void)a;
}
void rt_unregister_sync(MsActor *a) {
	(void)a;
}
MsValue rt_run_code(uint16_t actor_id, const uint8_t *code, uint32_t size, MsValue *args, int nargs) {
	(void)actor_id;
	(void)code;
	(void)size;
	(void)args;
	(void)nargs;
	return val_empty();
}
MsValue rt_call_function(uint32_t function_id, MsValue *args, int nargs) {
	(void)function_id;
	(void)args;
	(void)nargs;
	return val_empty();
}
void rt_quit(void) {
}
int rt_context_load_in_progress(uint16_t context_id) {
	(void)context_id;
	return 0;
}
void rt_set_multiple_sounds(int allow) {
	(void)allow;
}
void rt_set_multiple_streams(int allow) {
	(void)allow;
}
void audio_seq_start(MsAudioSeq *s) {
	(void)s;
}
void audio_seq_stop(MsAudioSeq *s) {
	(void)s;
}
void audio_seq_pause(MsAudioSeq *s) {
	(void)s;
}
void audio_seq_resume(MsAudioSeq *s) {
	(void)s;
}
uint32_t audio_seq_position_ms(const MsAudioSeq *s) {
	(void)s;
	return 0;
}
int rt_grant_sound_device(MsActor *a) {
	(void)a;
	return 1;
}
void rt_release_sound_device(MsActor *a) {
	(void)a;
}
void rt_draw_thick_line(int x0, int y0, int x1, int y1, int thickness, uint8_t colour) {
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
	(void)thickness;
	(void)colour;
}
uint8_t rt_find_color(uint8_t r, uint8_t g, uint8_t b) {
	(void)r;
	(void)g;
	(void)b;
	return 0;
}
void rt_perform_update_dirty(void) {
}
void audio_untrack(MsAudioSeq *s) {
	(void)s;
}
void rt_stage_remove(MsActor *a) {
	(void)a;
}
void rt_stage_resort(void) {
}
void video_effect_transition(MsValue *args, int nargs) {
	(void)args;
	(void)nargs;
}
void video_set_transition_on_sync(MsValue *args, int nargs) {
	(void)args;
	(void)nargs;
}
void audio_set_volume(double v) {
	(void)v;
}
double audio_get_volume(void) {
	return 1.0;
}
uint32_t rt_random(void) {
	return 4;
}
