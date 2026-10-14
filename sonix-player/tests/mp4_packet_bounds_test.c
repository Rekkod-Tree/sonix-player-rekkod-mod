// cc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. tests/mp4_packet_bounds_test.c src/system/decode/{mp4,growfile}.c -pthread -o /tmp/mp4_packet_bounds_test
// /tmp/mp4_packet_bounds_test variable-size-audio.m4a
#include "src/system/decode/mp4.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t be32(const unsigned char *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int main(int argc, char **argv) {
	assert(argc == 2);
	FILE *input = fopen(argv[1], "rb");
	assert(input && fseek(input, 0, SEEK_END) == 0);
	long length = ftell(input);
	assert(length > 32 && length < 4 * 1024 * 1024);
	rewind(input);
	unsigned char *bytes = malloc((size_t)length);
	assert(bytes && fread(bytes, 1, (size_t)length, input) == (size_t)length);
	fclose(input);
	long sample = 0;
	for (long i = 4; i + 20 < length; i++) {
		if (memcmp(bytes + i, "stsz", 4) == 0 && be32(bytes + i + 8) == 0) {
			uint32_t count = be32(bytes + i + 12);
			if (count && (uint64_t)i + 16 + 4 * (uint64_t)count <= (uint64_t)length &&
				be32(bytes + i - 4) == 20 + 4 * (uint64_t)count) { sample = i + 16; break; }
		}
	}
	assert(sample);
	const uint32_t values[] = {be32(bytes + sample), 0x80000000u, 0xffffffffu, 4 * 1024 * 1024 + 1, 0};
	char path[] = "/tmp/sonix-mp4-bounds-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		for (int b = 0; b < 4; b++) bytes[sample + b] = values[i] >> (24 - 8 * b);
		FILE *copy = fopen(path, "wb");
		assert(copy && fwrite(bytes, 1, length, copy) == (size_t)length);
		fclose(copy);
		mp4_file_t *file = mp4_open(path);
		assert(file);
		unsigned char small[4] = {1, 2, 3, 4};
		int result = mp4_read_audio_frame(file, 0, small, sizeof(small));
		assert(i == 0 ? result == -(int)values[i] : result == 0);
		assert(small[0] == 1 && small[3] == 4);
		mp4_close(file);
	}
	unlink(path);
	free(bytes);
	puts("PASS: MP4 packet bounds and negative-size API contract");
	return 0;
}
