// Whole actual module, real detached workers/conditions, inert cache/decode/GUI.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void *fixture_calloc(size_t, size_t);
static int fixture_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int fixture_wait(pthread_cond_t *, pthread_mutex_t *);
#define calloc fixture_calloc
#define pthread_create fixture_create
#define pthread_cond_wait fixture_wait
#include "ebookcovers_under_test.c"
#undef calloc
#undef pthread_create
#undef pthread_cond_wait

static pthread_t gui_thread;
static pthread_mutex_t observed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t observed = PTHREAD_COND_INITIALIZER;
static unsigned started, finished, notices, decoded, waiting_ready, waiting_idle;
static unsigned calloc_calls, fail_calloc, image_allocs, image_frees;
static bool hold_decode, release_decode, decode_oom, peek_oom, cache_miss, fail_create;
static bool keep_image, reenter;
static unsigned callbacks;
static int delivered_indices[8];
static char delivered_title[256];
static cover_image_t kept_image;
static uint32_t token, replacement_token;
static char files[3][256] = {"first.epub", "second.epub", "third.epub"};
static _Thread_local void *worker_arg;
typedef struct {void *(*fn)(void *); void *arg;} launch_t;
static launch_t launches[8];

static void notify(void) { pthread_cond_broadcast(&observed); }
static void await_at_least(unsigned *counter, unsigned expected) {
	struct timespec due;
	clock_gettime(CLOCK_REALTIME, &due); due.tv_sec += 2;
	pthread_mutex_lock(&observed_lock);
	while (*counter < expected) assert(pthread_cond_timedwait(&observed, &observed_lock, &due) == 0);
	pthread_mutex_unlock(&observed_lock);
}
static void *fixture_calloc(size_t n, size_t size) {
	assert(pthread_equal(pthread_self(), gui_thread));
	calloc_calls++;
	return fail_calloc == calloc_calls ? NULL : calloc(n, size);
}
static void *launch_worker(void *arg) {
	launch_t *launch = arg;
	worker_arg = launch->arg;
	launch->fn(launch->arg);
	pthread_mutex_lock(&observed_lock); finished++; notify(); pthread_mutex_unlock(&observed_lock);
	return NULL;
}
static int fixture_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
	assert(pthread_equal(pthread_self(), gui_thread));
	if (fail_create) return EAGAIN;
	int detached = 0; assert(!pthread_attr_getdetachstate(attr, &detached) && detached == PTHREAD_CREATE_DETACHED);
	assert(started < 8); launches[started] = (launch_t){fn, arg};
	int rc = pthread_create(thread, attr, launch_worker, &launches[started]);
	assert(!rc); started++; return rc;
}
static int fixture_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
	covers_job_t *job = worker_arg;
	assert(job && cond == &job->wake && mutex == &job->lock);
	pthread_mutex_lock(&observed_lock);
	if (job->result_ready) waiting_ready++; else waiting_idle++;
	notify(); pthread_mutex_unlock(&observed_lock);
	return pthread_cond_wait(cond, mutex);
}
void thread_be_background(const char *name) {
	assert(!pthread_equal(pthread_self(), gui_thread) && !strcmp(name, "ebookcovers"));
}

#define GUI_POST_MAX 32
#define GUI_LATEST_MAX 4
typedef struct {void (*cb)(void *); void *user;} gui_post_t;
static gui_post_t post_ring[GUI_POST_MAX], post_latest[GUI_LATEST_MAX];
static int post_count, post_head, post_wake_fd = -1;
static pthread_mutex_t post_lock = PTHREAD_MUTEX_INITIALIZER;
#include "actual_gui_bridge.h"
bool gui_post(void (*cb)(void *), void *user) {
	assert(user == NULL);
	bool admitted = actual_gui_post(cb, user);
	pthread_mutex_lock(&observed_lock); notices++; notify(); pthread_mutex_unlock(&observed_lock);
	return admitted;
}
static void inert_a(void *user) {(void)user;}
static void inert_b(void *user) {(void)user;}
static void inert_c(void *user) {(void)user;}
static void inert_d(void *user) {(void)user;}
static void fill_both_queues(void) {
	for (int i = 0; i < GUI_POST_MAX; i++) assert(actual_gui_post(inert_a, NULL));
	assert(gui_post_latest(inert_a, NULL) && gui_post_latest(inert_b, NULL));
	assert(gui_post_latest(inert_c, NULL) && gui_post_latest(inert_d, NULL));
}
static void allocate_image(cover_image_t *out) {
	out->pixels = malloc(8); assert(out->pixels); out->pixels[0] = 42; out->dsc.data = out->pixels;
	pthread_mutex_lock(&observed_lock); image_allocs++; pthread_mutex_unlock(&observed_lock);
}
bool cover_book_cached(const char *path, int w, int h, cover_image_t *out, bool *has_cover, char *title, size_t size) {
	assert(!pthread_equal(pthread_self(), gui_thread) && path && w == 48 && h == 64);
	pthread_mutex_lock(&observed_lock); decoded++; notify();
	while (hold_decode && !release_decode) pthread_cond_wait(&observed, &observed_lock);
	pthread_mutex_unlock(&observed_lock);
	if (decode_oom || peek_oom || cache_miss) return false;
	snprintf(title, size, "Cached %s", strrchr(path, '/') + 1);
	allocate_image(out); *has_cover = true; return true;
}
void *ebook_peek_cover(const char *path, uint32_t *size, char *title, size_t title_size) {
	(void)path;
	if (peek_oom) { *size = 0; return NULL; }
	snprintf(title, title_size, "Decoded title"); *size = 1; void *bytes = malloc(1); assert(bytes); return bytes;
}
bool cover_load_image_memory(const void *bytes, size_t size, int w, int h, cover_fit_t fit, cover_image_t *out) {
	assert(bytes && size == 1 && w == 48 && h == 64 && fit == COVER_FIT_COVER);
	if (decode_oom) return false;
	assert(cache_miss); allocate_image(out); return true;
}
void cover_book_store(const char *path, int w, int h, const cover_image_t *cover, const char *title) {
	(void)path; (void)w; (void)h; (void)cover; (void)title;
}
void cover_free(cover_image_t *image) {
	assert(image->pixels && image->pixels[0] == 42); free(image->pixels); memset(image, 0, sizeof(*image));
	pthread_mutex_lock(&observed_lock); image_frees++; notify(); pthread_mutex_unlock(&observed_lock);
}
static bool ready(int index, const char *title, const cover_image_t *cover) {
	assert(pthread_equal(pthread_self(), gui_thread));
	assert(callbacks < 8); delivered_indices[callbacks++] = index; snprintf(delivered_title, sizeof(delivered_title), "%s", title);
	assert(current_job && current_job->done[index] == COVERS_DELIVERED);
	if (decode_oom || peek_oom) assert(!cover);
	else assert(cover && cover->pixels && cover->pixels[0] == 42);
	if (keep_image) { kept_image = *cover; return true; }
	if (reenter) {
		reenter = false; ebookcovers_stop(token); await_at_least(&finished, 1);
		replacement_token = ebookcovers_start("/fixture/replaced", files, 1, 48, 64, ready);
		ebookcovers_want(replacement_token, 0, 0);
	}
	return false;
}
static uint32_t start(int count) { return ebookcovers_start("/fixture/books", files, count, 48, 64, ready); }
static void request(uint32_t active, int from, int to) { ebookcovers_want(active, from, to); }
static void stop_and_wait(uint32_t active, unsigned expected) {
	ebookcovers_stop(active); assert(!current_job); await_at_least(&finished, expected);
}
static void assert_no_extra_decode(unsigned expected) {
	pthread_mutex_lock(&observed_lock); assert(decoded == expected); pthread_mutex_unlock(&observed_lock);
}

int main(int argc, char **argv) {
	assert(argc == 2); gui_thread = pthread_self(); const char *test = argv[1];
	if (!strcmp(test, "thread-failure")) {
		fail_create = true; start(1); assert(!current_job && !started); ebookcovers_service_completion(); return 0;
	}
	if (!strncmp(test, "oom-start-", 10)) {
		fail_calloc = (unsigned)atoi(test + 10); start(1); assert(!current_job && !started);
		ebookcovers_service_completion(); fail_calloc = 0; token = start(1); stop_and_wait(token, 1); return 0;
	}
	if (!strcmp(test, "idle")) {
		token = start(1); await_at_least(&waiting_idle, 1); assert_no_extra_decode(0); stop_and_wait(token, 1); return 0;
	}
	if (!strcmp(test, "oom-decode")) decode_oom = true;
	if (!strcmp(test, "oom-peek")) peek_oom = true;
	if (!strcmp(test, "decode-success")) cache_miss = true;
	if (!strcmp(test, "full-both")) fill_both_queues();
	if (!strcmp(test, "stop-during-decode")) hold_decode = true;
	token = start(!strcmp(test, "visible-range") || !strcmp(test, "full-both") ? 3 : 1);
	request(token, !strcmp(test, "visible-range") ? 1 : 0, !strcmp(test, "full-both") ? 2 : !strcmp(test, "visible-range") ? 1 : 0);
	if (hold_decode) {
		await_at_least(&decoded, 1); ebookcovers_stop(token); assert(!current_job);
		pthread_mutex_lock(&observed_lock); assert(!finished); release_decode = true; notify(); pthread_mutex_unlock(&observed_lock);
		await_at_least(&finished, 1); assert(!callbacks && image_frees == 1); return 0;
	}
	await_at_least(&notices, 1); await_at_least(&waiting_ready, 1);
	assert(current_job); pthread_mutex_lock(&current_job->lock);
	assert(current_job->result_ready && current_job->done[!strcmp(test, "visible-range") ? 1 : 0] == COVERS_IN_FLIGHT);
	if (!strcmp(test, "stale-result")) current_job->result.gen--;
	pthread_mutex_unlock(&current_job->lock);
	if (!strcmp(test, "stop-waiting")) { stop_and_wait(token, 1); assert(image_frees == 1 && !callbacks); completion_wake(NULL); return 0; }
	if (!strcmp(test, "closing")) {
		ebookcovers_shutdown_completions(); assert(!current_job); await_at_least(&finished, 1);
		start(1); assert(!current_job && started == 1 && image_frees == 1); completion_wake(NULL); assert(!callbacks); return 0;
	}
	if (!strcmp(test, "stale-result")) {
		ebookcovers_service_completion(); assert(!callbacks && image_frees == 1); stop_and_wait(token, 1); return 0;
	}
	if (!strcmp(test, "keep")) keep_image = true;
	if (!strcmp(test, "reentrant-replace")) reenter = true;
	if (!strcmp(test, "full-both")) {
		request(token, 0, 2); await_at_least(&waiting_ready, 2); assert_no_extra_decode(1);
		for (unsigned i = 0; i < 3; i++) {
			ebookcovers_service_completion(); assert(callbacks == i + 1);
			if (i < 2) await_at_least(&notices, i + 2);
		}
		assert(decoded == 3 && image_frees == 3); stop_and_wait(token, 1); return 0;
	}
	ebookcovers_service_completion(); assert(callbacks == 1);
	if (keep_image) { assert(!image_frees && kept_image.pixels[0] == 42); cover_free(&kept_image); }
	if (!strcmp(test, "duplicate-wake")) {
		pthread_mutex_lock(&current_job->lock); assert(!current_job->result_ready); pthread_mutex_unlock(&current_job->lock);
		completion_wake(NULL); completion_wake(NULL); ebookcovers_service_completion(); assert(callbacks == 1);
	}
	if (!strcmp(test, "visible-range")) {
		assert(delivered_indices[0] == 1); request(token, 0, 0); await_at_least(&notices, 2);
		ebookcovers_service_completion(); assert(callbacks == 2 && delivered_indices[1] == 0 && decoded == 2);
	}
	if (!strcmp(test, "reentrant-replace")) {
		await_at_least(&notices, 2); ebookcovers_service_completion(); assert(callbacks == 2);
		stop_and_wait(replacement_token, 2);
	} else if (!strcmp(test, "stale-stop")) {
		stop_and_wait(token, 1); uint32_t fresh = start(1); request(fresh, 0, 0); await_at_least(&notices, 2);
		ebookcovers_stop(token); assert(current_job); ebookcovers_service_completion(); assert(callbacks == 2); stop_and_wait(fresh, 2);
	} else stop_and_wait(token, 1);
	assert(image_allocs == image_frees); if (peek_oom) assert(!strcmp(delivered_title, files[0]));
	return 0;
}
