// Normal PCM input restoration after a cancelled/short sink write.
// Production WAV/decoder rewind helpers and real speed/FIR reset code; the
// decoder seek is a cursor fixture. No ALSA device or player thread is started.
// cc -std=gnu11 -O2 -ffunction-sections -fdata-sections -I. -Ilvgl tests/audio_cancelled_input_test.c src/system/audio/speed.c src/system/audio/decimate.c src/system/audio/resample.c -Wl,--gc-sections -lasound -pthread -lm -o /tmp/audio_cancelled_input_test
#define _GNU_SOURCE
#include "src/system/audio/audio.c"
#include <assert.h>

typedef struct {
	uint64_t frame;
	bool refuse_seek;
} cursor_t;
static int crossfeed_resets;

int decoder_seek_to_frame(decoder_t *decoder, uint64_t frame) {
	cursor_t *cursor = (cursor_t *)decoder;
	if (cursor->refuse_seek)
		return 0;
	cursor->frame = frame;
	return 1;
}
void crossfeed_reset(void) { crossfeed_resets++; }
void eq_reset(void) { assert(!"must not change user EQ settings"); }
void peq_reset(void) { assert(!"must not change user PEQ settings"); }
void mseb_reset(void) { assert(!"must not change user MSEB settings"); }

static int fill_normal(void *user, short *samples, int frames) {
	cursor_t *cursor = user;
	for (int frame = 0; frame < frames; frame++) {
		for (int channel = 0; channel < 2; channel++) {
			samples[frame * 2 + channel] = (short)((int)(((cursor->frame + frame) * 17 + channel * 123) % 30000) - 15000);
		}
	}
	cursor->frame += (uint64_t)frames;
	return frames;
}

static bool all_zero(const void *data, size_t size) {
	const unsigned char *bytes = data;
	for (size_t i = 0; i < size; i++)
		if (bytes[i])
			return false;
	return true;
}

static void wav_input(void) {
	FILE *file = tmpfile();
	assert(file);
	unsigned char header[64] = {0};
	short ramp[64];
	for (int i = 0; i < 64; i++)
		ramp[i] = (short)(i * 17);
	assert(fwrite(header, 1, sizeof(header), file) == sizeof(header));
	assert(fwrite(ramp, sizeof(ramp), 1, file) == 1);
	assert(fflush(file) == 0);
	int64_t read_pos = 32;
	assert(fseeko(file, sizeof(header) + read_pos, SEEK_SET) == 0);
	// PREPARED/empty PCM: no frames reached it, so resume must read frame zero.
	assert(rewind_wav_input(file, sizeof(header), 0, &read_pos) && read_pos == 0);
	short got[16];
	assert(fread(got, sizeof(got), 1, file) == 1 && !memcmp(got, ramp, sizeof(got)));
	// A partial Bluetooth write accepted three stereo frames from this block.
	assert(rewind_wav_input(file, sizeof(header), 3 * 4, &read_pos) && read_pos == 12);
	assert(fread(got, sizeof(got), 1, file) == 1 && !memcmp(got, ramp + 6, sizeof(got)));
	assert(fclose(file) == 0);
	int pipe_fds[2];
	assert(pipe(pipe_fds) == 0);
	FILE *unseekable = fdopen(pipe_fds[0], "rb");
	assert(unseekable);
	read_pos = 99;
	int before = crossfeed_resets;
	assert(!rewind_wav_input(unseekable, 0, 0, &read_pos));
	assert(read_pos == 99 && crossfeed_resets == before);
	fclose(unseekable);
	close(pipe_fds[1]);
}

static void decoded_input(void) {
	cursor_t cursor = {0}, fresh_cursor = {.frame = 512};
	speed_t *stretch = speed_open(2, 44100), *fresh = speed_open(2, 44100);
	assert(stretch && fresh);
	speed_set_factor(stretch, 1.5);
	speed_set_factor(fresh, 1.5);
	short discarded[512], resumed[512], expected[512];
	uint64_t input_frames;
	assert(speed_pull(stretch, discarded, 256, fill_normal, &cursor, &input_frames) == 256);
	assert(cursor.frame > 512);
	decimator_t decim;
	assert(decimate_init(&decim, 4, 2) == 0);
	bt_resample_t resampler = {.on = true, .poly = true, .twice = true};
	assert(resample_init(&resampler.filter, 44100, 48000, 2) == 0);
	assert(interp2_init(&resampler.doubler, 2) == 0);
	int32_t samples[128], output[256];
	for (int i = 0; i < 128; i++)
		samples[i] = (i * 123 - 1000) * 256;
	assert(resample_s32(&resampler.filter, samples, 64, output) > 0);
	assert(interp2_s32(&resampler.doubler, samples, 64, output) == 128);
	assert(decimate_s32(&decim, samples, 64) == 16);
	assert(!all_zero(decim.stage[0].line, sizeof(decim.stage[0].line)));
	assert(!all_zero(resampler.filter.line, sizeof(resampler.filter.line)));
	assert(rewind_decoded_input((decoder_t *)&cursor, 512, stretch, &decim, &resampler));
	assert(cursor.frame == 512 && speed_factor(stretch) == 1.5);
	assert(decim.factor == 4 && decim.channels == 2);
	for (int i = 0; i < decim.stages; i++)
		assert(all_zero(decim.stage[i].line, sizeof(decim.stage[i].line)));
	assert(all_zero(resampler.filter.line, sizeof(resampler.filter.line)));
	assert(all_zero(resampler.doubler.line, sizeof(resampler.doubler.line)));
	assert(speed_pull(stretch, resumed, 256, fill_normal, &cursor, &input_frames) == 256);
	assert(speed_pull(fresh, expected, 256, fill_normal, &fresh_cursor, &input_frames) == 256);
	assert(!memcmp(resumed, expected, sizeof(resumed))); // no cached pre-pause speech
	// Failed reposition must not pretend the decoder or DSP moved successfully.
	cursor.refuse_seek = true;
	uint64_t before_frame = cursor.frame;
	int before = crossfeed_resets;
	resampler.filter.line[0][0] = 123;
	assert(!rewind_decoded_input((decoder_t *)&cursor, 0, stretch, &decim, &resampler));
	assert(cursor.frame == before_frame && crossfeed_resets == before && resampler.filter.line[0][0] == 123);
	assert(fill_normal(&cursor, resumed, 16) == 16 && cursor.frame == before_frame + 16);
	cursor.refuse_seek = false;
	// Native/DoP paths have no stretcher or rate converter; cursor restoration still works.
	bt_resample_t passthrough = {0};
	assert(rewind_decoded_input((decoder_t *)&cursor, 0, NULL, NULL, &passthrough) && cursor.frame == 0);
	speed_close(stretch);
	speed_close(fresh);
	resample_close(&resampler.filter);
}

int main(void) {
	wav_input();
	decoded_input();
	puts("PASS: unwritten/partial PCM cursor restoration, speed/FIR reset, failure and passthrough paths");
	return 0;
}
