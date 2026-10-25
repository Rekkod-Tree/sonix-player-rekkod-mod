// Generated production functions are inserted by download_priority_test.py.
#include "src/system/streaming/provider.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* HAS_PLAYBACK_API */
static atomic_int ym_save, zv_save, ym_playback, zv_playback, legacy_busy;
static atomic_int waits, closes;
static bool complete_during_wait;
bool yandexcache_busy(void) { return atomic_load(&ym_playback); }
bool zvukcache_busy(void) { return atomic_load(&zv_playback); }
bool yandexdl_busy(void) { return atomic_load(&ym_save); }
bool zvukdl_busy(void) { return atomic_load(&zv_save); }
static bool legacy_downloading(void) { return atomic_load(&legacy_busy); }
typedef struct {
	bool detached, abandon, st_open;
	char track_id[8];
	long src_pos;
	int st;
} download_t;
static _Thread_local download_t *thread_job;
static void http_stream_close(int *stream) {
	(void)stream;
	atomic_fetch_add(&closes, 1);
}
static void src_close(download_t *job) {
	if (job->st_open) {
		http_stream_close(&job->st);
		job->st_open = false;
	}
}
static void fixture_wait(unsigned delay) {
	assert(delay == 500000);
	int count = atomic_fetch_add(&waits, 1) + 1;
	if (complete_during_wait && count == 1)
		atomic_store(&ym_playback, 0);
	else
		thread_job->abandon = true;
}

/* PRODUCTION_REGISTRY */
#define usleep fixture_wait
/* PRODUCTION_CLIENTS */
#undef usleep

static stream_provider_t ym, zv, legacy;
static void reset(void) {
	provider_count = 0;
	ym = (stream_provider_t){.id = "yandex", .downloading = ym_provider_downloading};
	zv = (stream_provider_t){.id = "zvuk", .downloading = zv_provider_downloading};
	legacy = (stream_provider_t){.id = "legacy", .downloading = legacy_downloading};
	/* PLAYBACK_BINDINGS */
	stream_provider_register(&ym);
	stream_provider_register(&zv);
	stream_provider_register(&legacy);
	atomic_store(&ym_save, 1);
	atomic_store(&zv_save, 1);
	atomic_store(&ym_playback, 0);
	atomic_store(&zv_playback, 0);
	atomic_store(&legacy_busy, 0);
	atomic_store(&waits, 0);
	atomic_store(&closes, 0);
	complete_during_wait = false;
}
static void *run_save(void *arg) {
	download_t job = {.detached = true, .st_open = true, .src_pos = 42};
	strcpy(job.track_id, "test");
	thread_job = &job;
	if (arg)
		zv_give_way(&job);
	else
		ym_give_way(&job);
	assert(job.src_pos == 42);
	return NULL;
}
static bool pair_case(const char *name, bool playback, bool legacy_active, int expected_waits) {
	reset();
	atomic_store(&ym_playback, playback);
	atomic_store(&legacy_busy, legacy_active);
	pthread_t a, b;
	assert(pthread_create(&a, NULL, run_save, NULL) == 0);
	assert(pthread_create(&b, NULL, run_save, (void *)1) == 0);
	assert(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
	int actual = atomic_load(&waits);
	bool ok = actual == expected_waits && atomic_load(&closes) == expected_waits && stream_provider_any_downloading() && stream_provider_any_network_wanted();
	printf("%s: %s waits=%d expected=%d; storage/network protection retained\n", name, ok ? "PASS" : "FAIL", actual, expected_waits);
	return ok;
}
int main(void) {
	alarm(15);
	setvbuf(stdout, NULL, _IONBF, 0);
	bool ok = true;
	ok &= pair_case("two-background-saves", false, false, 0);
	ok &= pair_case("foreground-priority-and-cancel", true, false, 2);
	ok &= pair_case("legacy-provider-fallback", false, true, 2);
	reset();
	atomic_store(&ym_playback, 1);
	complete_during_wait = true;
	download_t resume = {.detached = true, .st_open = true, .src_pos = 99};
	strcpy(resume.track_id, "resume");
	thread_job = &resume;
	zv_give_way(&resume);
	bool resumed = !resume.abandon && !resume.st_open && resume.src_pos == 99 && atomic_load(&waits) == 1;
	printf("foreground-finishes: %s\n", resumed ? "PASS" : "FAIL");
	ok &= resumed;
	reset();
	atomic_store(&ym_playback, 1);
	download_t foreground = {.detached = false, .st_open = true};
	thread_job = &foreground;
	ym_give_way(&foreground);
	zv_give_way(&foreground);
	bool unblocked = atomic_load(&waits) == 0 && foreground.st_open;
	printf("foreground-never-yields: %s\n", unblocked ? "PASS" : "FAIL");
	ok &= unblocked;
#if HAS_PLAYBACK_API
	stream_provider_t empty = {.id = "empty"};
	assert(!stream_provider_playback_downloading(NULL));
	assert(!stream_provider_playback_downloading(&empty));
	atomic_store(&legacy_busy, 1);
	assert(stream_provider_playback_downloading(&legacy));
	atomic_store(&ym_playback, 0);
	assert(!stream_provider_playback_downloading(&ym) && ym.downloading());
#endif
	printf("priority contract: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
