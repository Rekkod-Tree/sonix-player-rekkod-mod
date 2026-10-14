// Compare a separately encoded ALAC file with its original signed S32 PCM.
// cc -O1 -g -fsanitize=address -I. tests/alac_file_test.c src/system/decode/{alacdec,mp4,growfile}.c src/system/decode/alac/alac.c -pthread -o /tmp/alac_file_test
// /tmp/alac_file_test fixture.m4a original.s32
#include "src/system/decode/alacdec.h"
#include "src/system/decode/mp4.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	assert(argc == 3);
	mp4_file_t *file = mp4_open(argv[1]);
	assert(file && mp4_audio_codec(file) == MP4_CODEC_ALAC);
	int config_size;
	const unsigned char *config = mp4_audio_config(file, &config_size);
	alacdec_t *decoder = alacdec_open(config, config_size);
	assert(decoder);
	int frames = alacdec_frame_samples(decoder), channels = alacdec_channels(decoder);
	int32_t *output = malloc((size_t)frames * channels * sizeof(*output));
	int32_t *expected = malloc((size_t)frames * channels * sizeof(*expected));
	uint32_t capacity = mp4_max_frame_size(file);
	if (!capacity) capacity = 4096;
	unsigned char *packet = malloc(capacity);
	assert(output && expected && packet);
	FILE *reference = fopen(argv[2], "rb");
	assert(reference);
	uint64_t total = 0;
	for (uint32_t i = 0; i < mp4_audio_frame_count(file); i++) {
		int length = mp4_read_audio_frame(file, i, packet, capacity);
		if (length < 0) {
			capacity = (uint32_t)-length;
			unsigned char *grown = realloc(packet, capacity);
			assert(grown);
			packet = grown;
			length = mp4_read_audio_frame(file, i, packet, capacity);
		}
		assert(length > 0);
		int count = alacdec_decode(decoder, packet, length, output, frames);
		assert(count > 0 && count <= frames);
		size_t samples = (size_t)count * channels;
		assert(fread(expected, sizeof(*expected), samples, reference) == samples);
		assert(memcmp(expected, output, samples * sizeof(*output)) == 0);
		total += count;
	}
	assert(fgetc(reference) == EOF);
	printf("PASS: %llu frames, %d channels, %d bits; every PCM sample matches\n",
		   (unsigned long long)total, channels, alacdec_bits(decoder));
	fclose(reference);
	free(packet); free(expected); free(output);
	alacdec_close(decoder);
	mp4_close(file);
	return 0;
}
