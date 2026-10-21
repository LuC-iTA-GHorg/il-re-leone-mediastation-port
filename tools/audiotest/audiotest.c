/* Unit test of the wave player formats (engine\audio_data.c): LIONKING.EXE accepts only PCM at
 * 11025/22050/44100 Hz, mono or stereo, 8 or 16 bits (1188:27ae, WAVEFORMAT tag 1 in
 * 1188:2ace); there is no ADPCM decoder in the executable. The samples are hand-made.
 * Build and run: tools\audiotest\audiotest.bat */
#include <stdio.h>
#include <string.h>

#include "runtime.h"

void audio_untrack(MsAudioSeq *s) { /* audio.c is not linked */
	(void)s;
}

static int failures;

static void check(const char *what, int got, int want) {
	if (got != want) {
		printf("FAIL %s: got %d, want %d\n", what, got, want);
		failures++;
	} else {
		printf("ok   %s\n", what);
	}
}

int main(void) {
	check("22050 Hz mono 16", audio_format_supported(22050, 1, 16), 1);
	check("11025 Hz stereo 8", audio_format_supported(11025, 2, 8), 1);
	check("44100 Hz stereo 16", audio_format_supported(44100, 2, 16), 1);
	check("22000 Hz refused", audio_format_supported(22000, 1, 16), 0);
	check("8000 Hz refused", audio_format_supported(8000, 1, 8), 0);
	check("3 channels refused", audio_format_supported(22050, 3, 16), 0);
	check("4-bit (ADPCM) refused", audio_format_supported(22050, 1, 4), 0);
	check("24-bit refused", audio_format_supported(22050, 1, 24), 0);

	/* 16-bit stereo: frames (L, R) = (0x1234, -2), (-32768, 32767) */
	uint8_t s16[] = {0x34, 0x12, 0xfe, 0xff, 0x00, 0x80, 0xff, 0x7f};
	uint8_t *chunks16[] = {s16};
	uint32_t sizes16[] = {sizeof(s16)};
	MsAudioSeq a;
	memset(&a, 0, sizeof(a));
	a.rate = 22050;
	a.channels = 2;
	a.bits = 16;
	a.chunks = chunks16;
	a.sizes = sizes16;
	a.nchunks = 1;
	int ok;
	check("16-bit frame 0 left", audio_pcm_sample(&a, 0, 0, 0, &ok), 0x1234);
	check("16-bit frame 0 right", audio_pcm_sample(&a, 0, 0, 1, &ok), -2);
	check("16-bit frame 1 left", audio_pcm_sample(&a, 0, 1, 0, &ok), -32768);
	check("16-bit frame 1 right", audio_pcm_sample(&a, 0, 1, 1, &ok), 32767);
	audio_pcm_sample(&a, 0, 2, 0, &ok);
	check("16-bit past the end", ok, 0);

	/* 8-bit mono, unsigned: 0x80 is silence */
	uint8_t s8[] = {0x80, 0x00, 0xff};
	uint8_t *chunks8[] = {s8};
	uint32_t sizes8[] = {sizeof(s8)};
	a.channels = 1;
	a.bits = 8;
	a.chunks = chunks8;
	a.sizes = sizes8;
	check("8-bit silence", audio_pcm_sample(&a, 0, 0, 0, &ok), 0);
	check("8-bit minimum", audio_pcm_sample(&a, 0, 1, 0, &ok), -32768);
	check("8-bit maximum", audio_pcm_sample(&a, 0, 2, 0, &ok), 32512);
	audio_pcm_sample(&a, 0, 3, 0, &ok);
	check("8-bit past the end", ok, 0);

	printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}
