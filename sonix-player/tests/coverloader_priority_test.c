// cc -O1 -g -std=gnu11 -I. -I./lvgl -DLV_CONF_INCLUDE_SIMPLE=1
// tests/coverloader_priority_test.c -pthread -o /tmp/coverloader_priority_test
// Actual worker and job selection; mocked image reads and scheduler only.
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
static atomic_bool screen_on = true, stop;
static atomic_int waiting, loads, applied_level = -1, scheduler_calls;
static int tracked_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
	atomic_fetch_add(&waiting, 1);
	int rc = pthread_cond_wait(condition, mutex);
	if (atomic_load(&stop)) {
		pthread_mutex_unlock(mutex);
		pthread_exit(NULL);
	}
	return rc;
}
#define pthread_cond_wait tracked_wait
#include "src/gui/nowplaying/coverloader.c"
#undef pthread_cond_wait

bool power_screen_is_on(void) { return atomic_load(&screen_on); }
void thread_set_background_level(bool visible) {
	assert(pthread_mutex_trylock(&lock) == 0); // Never lower priority holding UI's lock.
	pthread_mutex_unlock(&lock);
	atomic_store(&applied_level, visible);
	atomic_fetch_add(&scheduler_calls, 1);
}
void thread_be_low_priority(const char *name) {
	(void)name;
	thread_set_background_level(true);
}
static void loaded(void) {
	assert(atomic_load(&applied_level) == atomic_load(&screen_on));
	atomic_fetch_add(&loads, 1);
}
uint64_t cover_source_id(const char *path) {
	assert(path);
	loaded();
	return 1;
}
void cover_prefetch_player(const char *path, int w, int h) {
	assert(path && w && h);
	loaded();
}
void cover_free(cover_image_t *image) { assert(!image->pixels); }
bool cover_load_player_images(const char *path, int w, int h, cover_image_t *a, int sw, int sh, cover_image_t *b) {
	(void)path;
	(void)w;
	(void)h;
	(void)sw;
	(void)sh;
	memset(a, 0, sizeof(*a));
	memset(b, 0, sizeof(*b));
	loaded();
	return false;
}
bool cover_load_screensaver_images(const char *path, int w, int h, cover_image_t *a, int sw, int sh, cover_image_t *b) { return cover_load_player_images(path, w, h, a, sw, sh, b); }
bool cover_thumb_load(const char *path, bool dir, int size, cover_image_t *image) {
	(void)path;
	(void)dir;
	(void)size;
	memset(image, 0, sizeof(*image));
	loaded();
	return false;
}
bool cover_load_shared(const char *path, int w, int h, int size, cover_image_t *image) {
	(void)w;
	(void)h;
	return cover_thumb_load(path, false, size, image);
}
static void wait_for(atomic_int *counter, int target) {
	for (int i = 0; i < 2000 && atomic_load(counter) < target; i++)
		usleep(1000);
	assert(atomic_load(counter) >= target);
}
int main(void) {
	pthread_t thread;
	assert(pthread_create(&thread, NULL, worker, NULL) == 0);
	wait_for(&waiting, 1);
	for (int i = 0; i < 12; i++) {
		// Two same-state jobs, then change state while the worker sleeps.
		atomic_store(&screen_on, (i / 2) % 2);
		int calls_before = atomic_load(&scheduler_calls);
		switch (i % 6) {
		case 0:
			coverloader_request_player_id("id");
			break;
		case 1:
			coverloader_request_player("player", 20, 20, 0, 0);
			break;
		case 2:
			coverloader_request_screensaver("saver", 20, 20, 0, 0);
			break;
		case 3:
			coverloader_request(0, "thumb", 20);
			break;
		case 4:
			coverloader_request_shared(0, "shared", 20, 30, 30);
			break;
		case 5:
			coverloader_prefetch_player("prefetch", 20, 20);
			break;
		}
		wait_for(&loads, i + 1);
		wait_for(&waiting, i + 2);
		if (i % 2)
			assert(atomic_load(&scheduler_calls) == calls_before);
	}
	atomic_store(&stop, true);
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&wakeup);
	pthread_mutex_unlock(&lock);
	pthread_join(thread, NULL);
	puts("PASS artwork priority: screen transitions during sleep, all job types, no redundant scheduler calls, lock-free priority changes");
}
