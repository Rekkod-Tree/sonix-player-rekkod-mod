// Build from sonix-player/ (pass an owned stereo Vorbis test fixture):
// cc -std=gnu11 -O2 -Wall -Wextra -I. tests/vorbis_stream_test.c
//   src/system/decode/{vorbisdec,growstdio,growfile,stb_vorbis}.c
//   -lm -pthread -o /tmp/vorbis_stream_test
// /tmp/vorbis_stream_test /tmp/sonix-spotify-vorbis.ogg
#define _GNU_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "src/system/decode/vorbisdec.h"

static unsigned char *source;
static long source_size;
static char path[256];
static atomic_bool pause_requested;
static bool pause_seen;
static vorbisdec_t *reference;
static int peak_difference;
static uint64_t first_difference = UINT64_MAX;
static bool should_abort(void) { return atomic_load(&pause_requested); }

static void *writer(void *unused) {
	(void)unused;
	usleep(100000);
	atomic_store(&pause_requested, true);
	usleep(300000);
	FILE *file = fopen(path, "ab");
	assert(file);
	for (long at = 32768; at < source_size;) {
		long count = source_size - at < 4096 ? source_size - at : 4096;
		assert(fwrite(source + at, 1, (size_t)count, file) == (size_t)count);
		assert(fflush(file) == 0);
		at += count;
		growfile_progress(path, at);
		usleep(20000);
	}
	assert(fclose(file) == 0);
	growfile_finish(path, true);
	return NULL;
}

static uint64_t read_all(vorbisdec_t *decoder, uint64_t *frames, bool growing) {
	short out[2048];
	uint64_t hash = 1469598103934665603ULL;
	*frames = 0;
	for (int attempt = 0; attempt < 100000; attempt++) {
		uint64_t count = vorbisdec_read(decoder, 1024, out);
		if (atomic_exchange(&pause_requested, false)) {
			pause_seen = true;
		}
		if (!count) {
			if (growing && growfile_is_growing(path)) {
				usleep(1000);
				continue;
			}
			return hash;
		}
		*frames += count;
		short expected[2048];
		if (reference) {
			assert(vorbisdec_read(reference, count, expected) == count);
		}
		for (uint64_t i = 0; i < count * 2; i++) {
			if (reference) {
				int difference = abs((int)out[i] - expected[i]);
				if (difference && first_difference == UINT64_MAX) first_difference = *frames - count + i / 2;
				if (difference > peak_difference) peak_difference = difference;
			}
			hash ^= (uint16_t)out[i];
			hash *= 1099511628211ULL;
		}
	}
	assert(!"decoder did not terminate");
	return 0;
}

int main(int argc, char **argv) {
	assert(argc == 2);
	FILE *file = fopen(argv[1], "rb");
	assert(file);
	fseek(file, 0, SEEK_END);
	source_size = ftell(file);
	assert(source_size > 32768);
	rewind(file);
	source = malloc((size_t)source_size);
	assert(source && fread(source, 1, (size_t)source_size, file) == (size_t)source_size);
	rewind(file);
	vorbisdec_t *full = vorbisdec_open(file, NULL, NULL);
	assert(full && vorbisdec_rate(full) == 44100 && vorbisdec_channels(full) == 2);
	uint64_t expected_frames;
	uint64_t expected = read_all(full, &expected_frames, false);
	vorbisdec_close(full);

	snprintf(path, sizeof(path), "/tmp/sonix-vorbis-stream-%ld.ogg", (long)getpid());
	file = fopen(path, "wx");
	assert(file && fwrite(source, 1, 32768, file) == 32768);
	assert(fclose(file) == 0);
	growfile_announce(path, source_size);
	growfile_set_duration(path, 45000);
	growfile_progress(path, 32768);
	growfile_set_reader_abort_cb(should_abort);
	pthread_t thread;
	assert(pthread_create(&thread, NULL, writer, NULL) == 0);
	growfile_t *reader = growfile_open(path);
	assert(reader);
	growstdio_t *io = growstdio_open(reader);
	assert(io);
	vorbisdec_t *stream = vorbisdec_open(growstdio_file(io), io, reader);
	assert(stream && vorbisdec_length(stream) == 1984500);
	assert(vorbisdec_seek(stream, 44100));
	assert(vorbisdec_seek(stream, 0));
	assert(!vorbisdec_seek(stream, 44 * 44100));
	assert(vorbisdec_seek(stream, 0));
	file = fopen(argv[1], "rb"); assert(file);
	reference = vorbisdec_open(file, NULL, NULL); assert(reference);
	uint64_t actual_frames;
	uint64_t actual = read_all(stream, &actual_frames, true);
	vorbisdec_close(reference); reference = NULL;
	vorbisdec_close(stream);
	growfile_close(reader);
	pthread_join(thread, NULL);
	if (!pause_seen || actual_frames != expected_frames || actual != expected) {
		fprintf(stderr, "pause=%d frames=%llu/%llu first difference=%llu peak delta=%d\n", pause_seen,
			(unsigned long long)actual_frames, (unsigned long long)expected_frames,
			(unsigned long long)first_difference, peak_difference);
	}
	assert(pause_seen && actual_frames == expected_frames && actual == expected);

	// The caller may skip an ID3 prefix before handing ownership over.
	file = fopen(path, "wb+");
	assert(file && fwrite("ID3\4\0\0\0\0\0\0", 1, 10, file) == 10);
	assert(fwrite(source, 1, (size_t)source_size, file) == (size_t)source_size);
	assert(fflush(file) == 0 && fseek(file, 10, SEEK_SET) == 0);
	full = vorbisdec_open(file, NULL, NULL);
	assert(full);
	uint64_t tagged_frames;
	assert(read_all(full, &tagged_frames, false) == expected && tagged_frames == expected_frames);
	vorbisdec_close(full);

	file = fopen(path, "wb+");
	assert(file && fwrite("invalid", 1, 7, file) == 7 && fflush(file) == 0);
	rewind(file);
	int descriptor = fileno(file);
	assert(vorbisdec_open(file, NULL, NULL) == NULL);
	errno = 0;
	assert(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
	unlink(path);
	free(source);
	printf("Vorbis streaming PASS: %llu frames, pause boundary, seeks, ID3, failed-open ownership\n", (unsigned long long)actual_frames);
	return 0;
}
