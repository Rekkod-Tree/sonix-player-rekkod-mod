// Normal WSOLA accounting and PCM regression; no devices or player objects.
// cc -std=gnu11 -O2 -Wall -Wextra -I. tests/speed_accounting_test.c src/system/audio/speed.c src/system/core/sha256.c -o /tmp/speed_accounting_test
// /tmp/speed_accounting_test
// /tmp/speed_accounting_test --pcm  (compare stdout against the previous speed.c)
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/system/audio/speed.h"
#include "src/system/core/sha256.h"

typedef struct {
	uint64_t position;
	uint64_t end;
	int channels;
	unsigned fills;
} source_t;

static short sample_at(uint64_t frame, int channel) { return (short)((int)((frame * 17 + (frame / 101) * 53 + (unsigned)channel * 123) % 30000) - 15000); }

static int fill(void *user, short *out, int frames) {
	source_t *source = user;
	source->fills++;
	if ((uint64_t)frames > source->end - source->position)
		frames = (int)(source->end - source->position);
	for (int i = 0; i < frames; i++) {
		for (int channel = 0; channel < source->channels; channel++)
			out[i * source->channels + channel] = sample_at(source->position + (uint64_t)i, channel);
	}
	source->position += (uint64_t)frames;
	return frames;
}

static void cancelled_window(void) {
	speed_t *speed = speed_open(2, 44100);
	assert(speed);
	speed_set_factor(speed, 1.5);
	source_t source = {.end = 200000, .channels = 2};
	short out[4096 * 2];
	uint64_t consumed = 0;
	assert(speed_pull(speed, out, 4096, fill, &source, &consumed) == 4096);
	assert(consumed == 6144); // Previously charged three whole windows: 6354.
	uint64_t accepted = consumed;
	assert(source.position > accepted); // Decoder lookahead is not played audio.
	unsigned fills = source.fills;
	assert(speed_pull(speed, out, 64, fill, &source, &consumed) == 64);
	assert(source.fills == fills && consumed == 96); // Entirely from cached output.
	// This next block was cancelled by pause, so the sink accepted none of its
	// 96 source frames. The caller restores only the last accepted boundary.
	source.position = accepted;
	speed_reset(speed);
	assert(speed_pull(speed, out, 256, fill, &source, &consumed) == 256 && consumed == 384);
	assert(out[0] == sample_at(6144, 0) && out[1] == sample_at(6144, 1));
	assert(speed_factor(speed) == 1.5);
	speed_close(speed);
}

static void pull_partitioning(int rate, double factor) {
	speed_t *whole = speed_open(2, rate), *parts = speed_open(2, rate);
	assert(whole && parts);
	speed_set_factor(whole, factor);
	speed_set_factor(parts, factor);
	source_t a = {.end = 200000, .channels = 2}, b = a;
	short expected[4096 * 2], actual[4096 * 2];
	uint64_t expected_input = 0, actual_input = 0;
	assert(speed_pull(whole, expected, 4096, fill, &a, &expected_input) == 4096);
	const int requests[] = {1, 7, 64, 127, 511, 3386};
	int output = 0;
	for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
		uint64_t advance = 0;
		assert(speed_pull(parts, actual + output * 2, requests[i], fill, &b, &advance) == requests[i]);
		output += requests[i];
		actual_input += advance;
	}
	assert(output == 4096 && expected_input == actual_input);
	assert(!memcmp(expected, actual, sizeof(expected)));
	speed_close(whole);
	speed_close(parts);
}

static void bypass_and_tail(void) {
	speed_t *speed = speed_open(1, 44100);
	assert(speed);
	source_t source = {.end = 137, .channels = 1};
	short out[256];
	uint64_t consumed;
	assert(speed_pull(speed, out, 256, fill, &source, &consumed) == 137 && consumed == 137);
	for (int i = 0; i < 137; i++)
		assert(out[i] == sample_at((uint64_t)i, 0));
	assert(speed_pull(speed, out, 256, fill, &source, &consumed) == 0 && consumed == 0);
	speed_reset(speed);
	speed_set_factor(speed, 1.5);
	source.position = 0; // Shorter than one WSOLA window: the unchanged EOF tail.
	assert(speed_pull(speed, out, 64, fill, &source, &consumed) == 64 && consumed == 64);
	for (int i = 0; i < 64; i++)
		assert(out[i] == sample_at((uint64_t)i, 0));
	assert(speed_pull(speed, out, 256, fill, &source, &consumed) == 73 && consumed == 73);
	for (int i = 0; i < 73; i++)
		assert(out[i] == sample_at((uint64_t)i + 64, 0));
	assert(speed_pull(speed, out, 256, fill, &source, &consumed) == 0 && consumed == 0);
	speed_close(speed);
}

static void pcm_fingerprints(void) {
	const int rates[] = {11025, 44100, 48000};
	const double factors[] = {0.5, 1.0, 1.25, 1.5, 2.0};
	const int chunks[] = {1, 64, 127, 511, 4096};
	for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
		for (int channels = 1; channels <= 2; channels++) {
			for (size_t f = 0; f < sizeof(factors) / sizeof(factors[0]); f++) {
				speed_t *speed = speed_open(channels, rates[r]);
				assert(speed);
				speed_set_factor(speed, factors[f]);
				source_t source = {.end = (uint64_t)rates[r] + 137, .channels = channels};
				sha256_ctx hash;
				sha256_init(&hash);
				uint64_t frames = 0;
				unsigned turn = 0;
				for (;;) {
					short out[4096 * 2];
					uint64_t consumed;
					int got = speed_pull(speed, out, chunks[turn++ % 5], fill, &source, &consumed);
					assert(turn < 10000);
					if (!got)
						break;
					sha256_update(&hash, out, (size_t)got * channels * sizeof(short));
					frames += (uint64_t)got;
				}
				char digest[SHA256_HEX_LEN];
				sha256_final_hex(&hash, digest);
				printf("PCM rate=%d channels=%d factor=%.2f frames=%llu sha256=%s\n", rates[r], channels, factors[f], (unsigned long long)frames, digest);
				speed_close(speed);
			}
		}
	}
}

int main(int argc, char **argv) {
	if (argc == 2 && !strcmp(argv[1], "--pcm")) {
		pcm_fingerprints();
		return 0;
	}
	assert(argc == 1);
	cancelled_window();
	pull_partitioning(44100, 1.5);
	pull_partitioning(11025, 0.75);
	pull_partitioning(48000, 1.25);
	bypass_and_tail();
	puts("PASS: WSOLA delivered-input accounting, cached tail, cancellation/reset, chunking, 1x and EOF");
	return 0;
}
