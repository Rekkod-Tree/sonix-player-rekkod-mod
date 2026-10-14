// cc -std=gnu11 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -I. tests/wavpackdec_test.c src/system/decode/wavpackdec.c -lwavpack -lm -o /tmp/wavpackdec_test
// Uses the independent WavPack encoder to exercise the player's wrapper.
#include "src/system/decode/wavpackdec.h"
#include <wavpack/wavpack.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_block(void *id, void *data, int32_t count) {
	return fwrite(data, 1, (size_t)count, id) == (size_t)count;
}

static void encode(const char *path, int bits, bool floating, int32_t *samples, int count) {
	FILE *file = fopen(path, "wb");
	assert(file);
	WavpackContext *encoder = WavpackOpenFileOutput(write_block, file, NULL);
	assert(encoder);
	WavpackConfig config = {0};
	config.bytes_per_sample = (bits + 7) / 8;
	config.bits_per_sample = bits;
	config.num_channels = 1;
	config.channel_mask = 4;
	config.sample_rate = 48000;
	config.float_norm_exp = floating ? 127 : 0;
	assert(WavpackSetConfiguration64(encoder, &config, count, NULL));
	assert(WavpackPackInit(encoder));
	assert(WavpackPackSamples(encoder, samples, count));
	assert(WavpackFlushSamples(encoder));
	WavpackCloseFile(encoder);
	assert(fclose(file) == 0);
}

int main(void) {
	char directory[] = "/tmp/sonix-wavpack-test-XXXXXX";
	assert(mkdtemp(directory));
	char path[256];
	snprintf(path, sizeof(path), "%s/fixture.wv", directory);
	const int depths[] = {8, 16, 24, 32};
	for (unsigned b = 0; b < sizeof(depths) / sizeof(depths[0]); b++) {
		int bits = depths[b];
		int64_t scale = INT64_C(1) << (bits - 1);
		int32_t samples[] = {(int32_t)-scale, (int32_t)(-scale / 2), -1, 0, 1, (int32_t)(scale / 2), (int32_t)(scale - 1)};
		int32_t expected[7];
		for (int i = 0; i < 7; i++) expected[i] = (int32_t)((int64_t)samples[i] * (INT64_C(1) << (32 - bits)));
		encode(path, bits, false, samples, 7);
		wavpackdec_t *decoder = wavpackdec_open(path);
		assert(decoder && wavpackdec_bits(decoder) == bits);
		int32_t output[9] = {0};
		output[8] = 12345;
		assert(wavpackdec_read_s32(decoder, 3, output) == 3);
		assert(wavpackdec_read_s32(decoder, 5, output + 3) == 4);
		assert(memcmp(output, expected, sizeof(expected)) == 0 && output[8] == 12345);
		assert(wavpackdec_seek(decoder, 0));
		short narrow[7];
		assert(wavpackdec_read_s16(decoder, 7, narrow) == 7);
		for (int i = 0; i < 7; i++) assert(narrow[i] == (short)(expected[i] >> 16));
		wavpackdec_close(decoder);
	}
	const float floating[] = {-INFINITY, -1.0f, -0.25f, 0.0f, 0.25f, 1.0f, INFINITY, NAN};
	int32_t words[8];
	memcpy(words, floating, sizeof(words));
	encode(path, 32, true, words, 8);
	wavpackdec_t *decoder = wavpackdec_open(path);
	assert(decoder && wavpackdec_bits(decoder) == 24);
	const int32_t expected[] = {INT32_MIN, INT32_MIN, -536870912, 0, 536870912, 2147483392, 2147483392, 0};
	int32_t output[8];
	assert(wavpackdec_read_s32(decoder, 8, output) == 8);
	assert(memcmp(output, expected, sizeof(output)) == 0);
	wavpackdec_close(decoder);
	unlink(path);
	rmdir(directory);
	puts("PASS: WavPack 8/16/24/32-bit, float bounds/NaN, short reads and seeking");
	return 0;
}
