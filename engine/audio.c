/* Sound playback: every playing MsAudioSeq gets a voice of a small software mixer (SDL2 audio
 * callback). The chunks of a sequence are played back to back (AudioSequence in the original).
 * The end of a sound is noticed by audio_service(), called once per frame by the event loop
 * ("serviceSounds" in the original), which reports it to the actor. */
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "script.h"

#define MAX_VOICES 32
#define OUT_RATE 44100

typedef struct {
	MsAudioSeq *seq; /* NULL = free */
	int chunk;
	uint32_t pos_fp; /* position in the chunk, in samples, 16.16 fixed point */
	uint32_t step;   /* source rate / output rate, 16.16 */
	int paused;
	int finished;
} Voice;

static Voice voices[MAX_VOICES];
static SDL_AudioDeviceID dev;
static double volume = 1.0;

static void callback(void *user, Uint8 *stream, int len) {
	(void)user;
	int16_t *out = (int16_t *)stream;
	int frames = len / 4; /* stereo 16-bit */
	int32_t *mix = calloc((size_t)frames * 2, sizeof(int32_t));
	int vol = (int)(volume * 256);
	for (int v = 0; v < MAX_VOICES; v++) {
		Voice *vc = &voices[v];
		MsAudioSeq *s = vc->seq;
		if (!s || vc->paused || vc->finished)
			continue;
		for (int i = 0; i < frames; i++) {
			int ok;
			int16_t l = 0, r = 0;
			for (;;) {
				if (vc->chunk >= s->nchunks) {
					vc->finished = 1;
					break;
				}
				l = audio_pcm_sample(s, vc->chunk, vc->pos_fp >> 16, 0, &ok);
				if (ok) {
					r = s->channels == 2 ? audio_pcm_sample(s, vc->chunk, vc->pos_fp >> 16, 1, &ok) : l;
					break;
				}
				vc->chunk++;
				vc->pos_fp = 0;
			}
			if (vc->finished)
				break;
			mix[i * 2] += l;
			mix[i * 2 + 1] += r;
			vc->pos_fp += vc->step;
			s->played += (vc->pos_fp >> 16) - ((vc->pos_fp - vc->step) >> 16);
		}
	}
	for (int i = 0; i < frames * 2; i++) {
		int32_t m = mix[i] * vol / 256;
		if (m > 32767)
			m = 32767;
		if (m < -32768)
			m = -32768;
		out[i] = (int16_t)m;
	}
	free(mix);
}

int audio_init(void) {
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = OUT_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = callback;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!dev)
		return 0;
	SDL_PauseAudioDevice(dev, 0);
	return 1;
}

void audio_shutdown(void) {
	if (dev)
		SDL_CloseAudioDevice(dev);
	dev = 0;
}

/* Sequences that were started and are no longer producing sound end here. Sequences with no
 * voice (empty, or no audio device) end immediately, like an empty stream in the original. */
static MsAudioSeq *playing[256];
static int nplaying;

void audio_track(MsAudioSeq *s) {
	for (int i = 0; i < nplaying; i++)
		if (playing[i] == s)
			return;
	if (nplaying < 256)
		playing[nplaying++] = s;
}

void audio_untrack(MsAudioSeq *s) {
	/* also frees its voice: the mixer must never read a sequence that is being freed */
	if (dev) {
		SDL_LockAudioDevice(dev);
		for (int v = 0; v < MAX_VOICES; v++)
			if (voices[v].seq == s)
				voices[v].seq = NULL;
		SDL_UnlockAudioDevice(dev);
	}
	s->voice = -1;
	for (int i = 0; i < nplaying; i++)
		if (playing[i] == s) {
			playing[i] = playing[--nplaying];
			return;
		}
}

static void notify(MsAudioSeq *s, int state, int reason) {
	s->state = state;
	if (s->on_state)
		s->on_state(s, state, reason, s->user);
}

void audio_seq_start(MsAudioSeq *s) {
	if (s->state != AUDIO_STOPPED && s->state != 0)
		return;
	s->voice = -1;
	s->played = 0;
	if (!audio_format_supported(s->rate, s->channels, s->bits)) {
		/* the wave device cannot be opened (1188:27ae / 1188:2ace fail): the sound fails */
		ms_warn("audio: format %u Hz, %u channels, %u bits cannot be played", s->rate,
			s->channels, s->bits);
		notify(s, AUDIO_STOPPED, STOP_FAILURE);
		return;
	}
	audio_track(s);
	notify(s, AUDIO_PLAYING, STOP_NONE);
	if (s->nchunks == 0 || !dev)
		return; /* nothing to play: ends at the next service */
	SDL_LockAudioDevice(dev);
	for (int v = 0; v < MAX_VOICES; v++)
		if (!voices[v].seq) {
			voices[v].seq = s;
			voices[v].chunk = 0;
			voices[v].pos_fp = 0;
			voices[v].step = (uint32_t)(((uint64_t)(s->rate ? s->rate : 22050) << 16) / OUT_RATE);
			voices[v].paused = 0;
			voices[v].finished = 0;
			s->played = 0;
			s->voice = v;
			break;
		}
	SDL_UnlockAudioDevice(dev);
	if (s->voice < 0)
		ms_warn("audio: no free voice");
}

static void release_voice(MsAudioSeq *s) {
	if (s->voice >= 0 && dev) {
		SDL_LockAudioDevice(dev);
		if (voices[s->voice].seq == s)
			voices[s->voice].seq = NULL;
		SDL_UnlockAudioDevice(dev);
	}
	s->voice = -1;
}

void audio_seq_stop(MsAudioSeq *s) {
	release_voice(s);
	audio_untrack(s);
	notify(s, AUDIO_STOPPED, STOP_SCRIPT);
}

void audio_seq_pause(MsAudioSeq *s) {
	if (s->voice >= 0 && dev) {
		SDL_LockAudioDevice(dev);
		voices[s->voice].paused = 1;
		SDL_UnlockAudioDevice(dev);
	}
	notify(s, AUDIO_PAUSED, STOP_NONE);
}

void audio_seq_resume(MsAudioSeq *s) {
	if (s->voice >= 0 && dev) {
		SDL_LockAudioDevice(dev);
		voices[s->voice].paused = 0;
		SDL_UnlockAudioDevice(dev);
	}
	notify(s, AUDIO_PLAYING, STOP_NONE);
}

void audio_service(void) {
	MsAudioSeq *done[256];
	int ndone = 0;
	if (dev)
		SDL_LockAudioDevice(dev);
	for (int i = 0; i < nplaying; i++) {
		MsAudioSeq *s = playing[i];
		if (s->state != AUDIO_PLAYING)
			continue;
		int ended = s->voice < 0 || voices[s->voice].seq != s || voices[s->voice].finished;
		if (ended)
			done[ndone++] = s;
	}
	if (dev)
		SDL_UnlockAudioDevice(dev);
	for (int i = 0; i < ndone; i++) {
		MsAudioSeq *s = done[i];
		/* the sequence may have been stopped by a callback of a previous one */
		int still = 0;
		for (int k = 0; k < nplaying; k++)
			if (playing[k] == s)
				still = 1;
		if (!still || s->state != AUDIO_PLAYING)
			continue;
		release_voice(s);
		audio_untrack(s);
		notify(s, AUDIO_STOPPED, STOP_END);
	}
}

void audio_set_volume(double v) {
	volume = v;
}

double audio_get_volume(void) {
	return volume;
}

/* Focus loss pauses the whole wave device (waveOutPause, LIONKING.EXE 1188:219a) and
 * reactivation restarts it (waveOutRestart, 1188:2202): every voice stops where it is. */
void audio_device_pause(int pause) {
	if (dev)
		SDL_PauseAudioDevice(dev, pause);
}

uint32_t audio_seq_position_ms(const MsAudioSeq *s) {
	uint32_t played;
	if (dev)
		SDL_LockAudioDevice(dev);
	played = s->played;
	if (dev)
		SDL_UnlockAudioDevice(dev);
	uint32_t rate = s->rate ? s->rate : 22050;
	return (uint32_t)((uint64_t)played * 1000 / rate);
}
