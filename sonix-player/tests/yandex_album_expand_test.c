// Run through yandex_album_expand_test.py, which inserts the production branch.
#include "src/system/library/audiobookdb.h"
#include "src/system/streaming/yandexdl.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* PRODUCTION_ENTRY_TYPE */

#define QUEUE_MAX 300
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned generation = 7;
static entry_t *queue;
static int queue_len;
static yandexdl_status_t status;
static int response_count, unavailable_index, failed_events, invalid_moves;
static bool fail_tracks, fail_growth;
static size_t queue_bytes;

static const char *tr(const char *key) { return key; }
const char *yandex_last_error(void) { return "metadata request failed"; }
int yandex_album_tracks(const char *id, yandex_track_t *out, int maximum) {
	assert(!strcmp(id, "album") && maximum == 150);
	for (int i = 0; i < response_count; ++i) {
		snprintf(out[i].id, sizeof(out[i].id), "track%d", i);
		out[i].available = i != unavailable_index;
	}
	return response_count;
}
static void tell(yandexdl_event_t event, int count, const char *text) {
	assert(event == YANDEXDL_FAILED && count == 1 && text && *text);
	++failed_events;
}
static void *test_calloc(size_t count, size_t bytes) { return fail_tracks ? NULL : calloc(count, bytes); }
static void *test_realloc(void *old, size_t bytes) {
	if (fail_growth)
		return NULL;
	void *next = realloc(old, bytes);
	if (next)
		queue_bytes = bytes;
	return next;
}
static void *test_memmove(void *dst, const void *src, size_t bytes) {
	uintptr_t base = (uintptr_t)queue, d = (uintptr_t)dst, s = (uintptr_t)src;
	if (d < base || s < base || d - base > queue_bytes || s - base > queue_bytes || bytes > queue_bytes - (d - base) || bytes > queue_bytes - (s - base)) {
		++invalid_moves;
		return dst;
	}
	return memmove(dst, src, bytes);
}
#define calloc test_calloc
#define realloc test_realloc
#define memmove test_memmove
static void expand(entry_t e, unsigned asked) { /* PRODUCTION_EXPANSION */ }
#undef calloc
#undef realloc
#undef memmove

static bool run_case(const char *name, int pending, int answer, unsigned asked, bool allocation_failure, bool growth_failure, int unavailable, int expected_added, int expected_errors) {
	queue_len = pending;
	queue_bytes = (size_t)pending * sizeof(*queue);
	queue = pending ? calloc((size_t)pending, sizeof(*queue)) : NULL;
	assert(!pending || queue);
	for (int i = 0; i < pending; ++i) {
		snprintf(queue[i].track.id, sizeof(queue[i].track.id), "pending%d", i);
		strcpy(queue[i].book_folder, "Existing/Books");
	}
	status = (yandexdl_status_t){.queued = pending};
	response_count = answer;
	failed_events = invalid_moves = 0;
	fail_tracks = allocation_failure;
	fail_growth = growth_failure;
	unavailable_index = unavailable;
	entry_t entry = {.album = true};
	strcpy(entry.track.album_id, "album");
	strcpy(entry.track.album, "Test album");
	strcpy(entry.book_folder, "New/Books");
	expand(entry, asked);
	bool ok = invalid_moves == 0 && queue_len == pending + expected_added && status.queued == queue_len && failed_events == expected_errors && status.failed == expected_errors;
	for (int i = 0; ok && i < pending; ++i) {
		char expected[32];
		snprintf(expected, sizeof(expected), "pending%d", i);
		ok = !strcmp(queue[expected_added + i].track.id, expected) && !strcmp(queue[expected_added + i].book_folder, "Existing/Books");
	}
	int track = 0;
	for (int i = 0; ok && i < expected_added; ++i, ++track) {
		if (track == unavailable)
			++track;
		char expected[32];
		snprintf(expected, sizeof(expected), "track%d", track);
		ok = !strcmp(queue[i].track.id, expected) && !strcmp(queue[i].book_folder, "New/Books");
	}
	printf("%s: %s; invalid_moves=%d, queue=%d, errors=%d\n", name, ok ? "PASS" : "FAIL", invalid_moves, queue_len, failed_events);
	free(queue);
	queue = NULL;
	return ok;
}
int main(void) {
	alarm(15);
	bool ok = true;
	ok &= run_case("metadata-error-with-following-jobs", 2, -1, 7, false, false, -1, 0, 1);
	ok &= run_case("track-allocation-error", 2, 3, 7, true, false, -1, 0, 1);
	ok &= run_case("error-with-empty-queue", 0, -1, 7, false, false, -1, 0, 1);
	ok &= run_case("empty-album", 2, 0, 7, false, false, -1, 0, 0);
	ok &= run_case("cancelled-generation", 2, 3, 6, false, false, -1, 0, 0);
	ok &= run_case("full-queue", 300, 3, 7, false, false, -1, 0, 0);
	ok &= run_case("ordered-insertion", 2, 3, 7, false, false, -1, 3, 0);
	ok &= run_case("unavailable-track", 2, 3, 7, false, false, 1, 2, 0);
	ok &= run_case("bounded-insertion", 298, 3, 7, false, false, -1, 2, 0);
	ok &= run_case("queue-allocation-error", 2, 3, 7, false, true, -1, 0, 0);
	return ok ? 0 : 1;
}
