/* Audio sequences: the parameters and chunks of a sound or of the soundtrack of a movie.
 * Playback is in audio.c (SDL2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

void audio_seq_read_params(MsAudioSeq *s, MsChunk *c) {
	s->chunk_count = ms_t_u16(c);
	s->rate = ms_t_u32(c);
	s->channels = ms_t_u16(c);
	s->bits = ms_t_u16(c);
	if (getenv("MS_AUDIO_INFO"))
		fprintf(stderr, "audio: chunks %u rate %u channels %u bits %u\n", s->chunk_count, s->rate,
			s->channels, s->bits);
}

void audio_seq_add_chunk(MsAudioSeq *s, MsChunk *c) {
	size_t n = ms_remaining(c);
	s->chunks = realloc(s->chunks, sizeof(uint8_t *) * (size_t)(s->nchunks + 1));
	s->sizes = realloc(s->sizes, sizeof(uint32_t) * (size_t)(s->nchunks + 1));
	uint8_t *d = malloc(n ? n : 1);
	ms_read(c, d, n);
	s->chunks[s->nchunks] = d;
	s->sizes[s->nchunks] = (uint32_t)n;
	s->nchunks++;
}

void audio_seq_free(MsAudioSeq *s) {
	audio_untrack(s);
	for (int i = 0; i < s->nchunks; i++)
		free(s->chunks[i]);
	free(s->chunks);
	free(s->sizes);
	s->chunks = NULL;
	s->sizes = NULL;
	s->nchunks = 0;
}

/* The only formats the original can play (wave player, LIONKING.EXE 1188:27ae): PCM
 * (WAVEFORMAT tag 1, 1188:2ace) at 11025, 22050 or 44100 Hz, mono or stereo, 8 or 16 bits;
 * the combination must also be in the capabilities of the wave device (DS:008E table of
 * WAVE_FORMAT_* flags). There is no ADPCM or other codec in the executable. Anything else
 * cannot be opened and the sound fails. */
int audio_format_supported(uint32_t rate, uint16_t channels, uint16_t bits) {
	if (rate != 11025 && rate != 22050 && rate != 44100)
		return 0;
	if (channels != 1 && channels != 2)
		return 0;
	return bits == 8 || bits == 16;
}

/* Sample `channel` of frame `frame` of a chunk: 8-bit unsigned or 16-bit signed little endian,
 * channels interleaved (the WAVE PCM layout that waveOutWrite takes as it is). */
int16_t audio_pcm_sample(const MsAudioSeq *s, int chunk, uint32_t frame, int channel, int *ok) {
	uint32_t ch = s->channels == 2 ? 2 : 1;
	uint32_t idx = frame * ch + (uint32_t)(channel < (int)ch ? channel : 0);
	*ok = 0;
	if (s->bits == 16) {
		if (idx >= s->sizes[chunk] / 2)
			return 0;
		const uint8_t *p = s->chunks[chunk] + idx * 2;
		*ok = 1;
		return (int16_t)(p[0] | p[1] << 8);
	}
	if (s->bits == 8) {
		if (idx >= s->sizes[chunk])
			return 0;
		*ok = 1;
		return (int16_t)((s->chunks[chunk][idx] - 128) * 256);
	}
	return 0;
}
