// cc -std=gnu11 -O1 -g -fsanitize=address -I. tests/alacdec_test.c src/system/decode/alacdec.c src/system/decode/alac/alac.c -o /tmp/alacdec_test
#include "src/system/decode/alacdec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_ALLOC_FAULTS
static int fail_allocation;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size) {
	if (fail_allocation && --fail_allocation == 0) return NULL;
	return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
	if (fail_allocation && --fail_allocation == 0) return NULL;
	return __real_calloc(count, size);
}
#endif

typedef struct { unsigned char bytes[1024]; unsigned bit; } bits_t;
static void put(bits_t *b, uint32_t value, unsigned count) {
	assert(b->bit + count <= sizeof(b->bytes) * 8);
	for (unsigned i = count; i > 0; i--, b->bit++)
		b->bytes[b->bit / 8] |= ((value >> (i - 1)) & 1) << (7 - b->bit % 8);
}

static void cookie(unsigned char data[24], int depth, int channels) {
	memset(data, 0, 24);
	data[3] = 8;
	data[5] = depth;
	data[6] = 40;
	data[7] = 10;
	data[8] = 14;
	data[9] = channels;
	data[22] = 0xac;
	data[23] = 0x44;
}

static bits_t packet(int depth, int channels, bool compressed, int32_t *expected) {
	bits_t b = {0};
	put(&b, channels - 1, 3);
	put(&b, 0, 4 + 12);
	put(&b, 1, 1); // explicit frame size
	put(&b, 0, 2); // no uncompressed low bytes
	put(&b, !compressed, 1);
	put(&b, 8, 32);
	if (compressed) {
		put(&b, 0, 16); // interlacing fields (unused in mono)
		for (int c = 0; c < channels; c++) put(&b, 0, 16); // no prediction coefficients
		for (int c = 0; c < channels; c++) {
			put(&b, 0, 1); // first zero sample, k=1
			put(&b, 0, 1); put(&b, 8, 4); // seven following zero samples
		}
		memset(expected, 0, 8 * channels * sizeof(*expected));
	} else {
		for (int i = 0; i < 8 * channels; i++) {
			int32_t value = i % 2 ? 12345 : -12345;
			put(&b, (uint32_t)value, depth);
			expected[i] = (int32_t)((int64_t)value * (INT64_C(1) << (32 - depth)));
		}
	}
	return b;
}

int main(void) {
#ifdef TEST_ALLOC_FAULTS
	unsigned char fault_config[24];
	cookie(fault_config, 16, 2);
	for (int allocation = 1; allocation <= 9; allocation++) {
		fail_allocation = allocation;
		assert(alacdec_open(fault_config, sizeof(fault_config)) == NULL);
		fail_allocation = 0;
	}
#endif
	for (int depth = 16; depth <= 24; depth += 8) {
		for (int channels = 1; channels <= 2; channels++) {
			unsigned char config[24];
			cookie(config, depth, channels);
			alacdec_t *decoder = alacdec_open(config, sizeof(config));
			assert(decoder);
			int32_t output[16], expected[16];
			bits_t short_frame = {0};
			put(&short_frame, channels - 1, 3); put(&short_frame, 0, 16);
			put(&short_frame, 1, 1); put(&short_frame, 0, 3); put(&short_frame, 1, 32);
			put(&short_frame, 0, 16);
			for (int c = 0; c < channels; c++) {
				put(&short_frame, 0, 11); put(&short_frame, 15, 5);
				for (int j = 0; j < 15; j++) put(&short_frame, 0, 16);
			}
			for (int c = 0; c < channels; c++) put(&short_frame, 0, 1);
			assert(alacdec_decode(decoder, short_frame.bytes, (short_frame.bit + 7) / 8, output, 8) == 1);
			for (int c = 0; c < channels; c++) assert(output[c] == 0);
			bits_t wrong = packet(depth, 3 - channels, false, expected);
			assert(alacdec_decode(decoder, wrong.bytes, (wrong.bit + 7) / 8, output, 8) == 0);
			for (int compressed = 0; compressed < 2; compressed++) {
				bits_t b = packet(depth, channels, compressed, expected);
				int bytes = (b.bit + 7) / 8;
				assert(alacdec_decode(decoder, b.bytes, bytes, output, 8) == 8);
				assert(memcmp(output, expected, 8 * channels * sizeof(*output)) == 0);
				// Any missing byte in this packet must be rejected, not padded into music.
				for (int length = 1; length < bytes; length++)
					assert(alacdec_decode(decoder, b.bytes, length, output, 8) == 0);
			}
			uint32_t random = 0x452821e6;
			for (int attempt = 0; attempt < 3000; attempt++) {
				unsigned char bytes[256];
				for (unsigned i = 0; i < sizeof(bytes); i++) {
					random ^= random << 13; random ^= random >> 17; random ^= random << 5;
					bytes[i] = random;
				}
				bytes[0] = (bytes[0] & 0x1f) | ((channels - 1) << 5);
				bytes[2] &= ~0x10; // use the bounded cookie's frame size
				int count = alacdec_decode(decoder, bytes, 3 + attempt % 254, output, 8);
				assert(count >= 0 && count <= 8);
			}
			alacdec_close(decoder);
			config[8] = 255;
			assert(alacdec_open(config, sizeof(config)) == NULL);
		}
	}
	puts("PASS: ALAC raw PCM, compressed zeros, truncation, channel mismatch and cookie bounds");
	return 0;
}
