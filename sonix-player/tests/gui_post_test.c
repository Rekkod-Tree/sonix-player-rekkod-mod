// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/gui_post_test.c -pthread -Wl,--gc-sections -o /tmp/gui_post_test
#define _GNU_SOURCE
#include "src/gui/shell/gui.c"
#include <assert.h>
#include <poll.h>
bool clock_poll(void) { return false; }
void topbar_refresh_clock(void) {}
static int ordinary, completions, volume;
static void notice(void *unused) {
	assert((intptr_t)unused == ordinary); // Ring remains FIFO across passes.
	ordinary++;
}
static void completion(void *unused) {
	(void)unused;
	completions++;
}
static void level(void *value) {
	volume = (int)(intptr_t)value;
}
static bool ringing(void) {
	struct pollfd p = {.fd = post_wake_fd, .events = POLLIN};
	return poll(&p, 1, 0) == 1 && (p.revents & POLLIN);
}
static int reposts, latest_a_count, latest_b_count;
static void repost(void *unused) {
	(void)unused;
	if (++reposts < 10000)
		assert(gui_post(repost, NULL));
}
static void latest_a(void *unused) {
	(void)unused;
	if (++latest_a_count < 100)
		assert(gui_post_latest(latest_a, NULL));
}
static void latest_b(void *unused) {
	(void)unused;
	if (++latest_b_count < 100)
		assert(gui_post_latest(latest_b, NULL));
}
static int slow_count;
static void slow(void *unused) {
	(void)unused;
	slow_count++;
	usleep(POST_DRAIN_US * 2);
}
int main(void) {
	post_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	assert(post_wake_fd >= 0);
	for (int i = 0; i < GUI_POST_MAX; i++)
		assert(gui_post(notice, (void *)(intptr_t)i));
	assert(!gui_post(notice, NULL));
	assert(!gui_post_or_free(notice, malloc(128)));
	assert(gui_post_latest(completion, NULL));
	assert(gui_post_latest(completion, NULL));
	assert(gui_post_latest(level, (void *)(intptr_t)35));
	assert(gui_post_latest(level, (void *)(intptr_t)44));
	assert(ringing());
	post_drain_cb(NULL);
	assert(ordinary < GUI_POST_MAX && ordinary > 0 && completions == 1 && volume == 44);
	assert(ringing()); // Tail retained even though this pass read the eventfd.
	while (post_count)
		post_drain_cb(NULL);
	assert(ordinary == 32 && completions == 1 && volume == 44 && post_count == 0);
	post_drain_cb(NULL); // Clears any harmless coalesced producer ring.
	assert(!ringing());

	assert(gui_post(repost, NULL));
	assert(gui_post_latest(latest_a, NULL));
	assert(gui_post_latest(latest_b, NULL));
	post_drain_cb(NULL);
	assert(reposts > 0 && latest_a_count > 0 && latest_b_count > 0);
	assert(reposts + latest_a_count + latest_b_count <= POST_DRAIN_MAX);
	int passes = 1;
	while (reposts < 10000 || latest_a_count < 100 || latest_b_count < 100) {
		assert(ringing());
		int before = reposts + latest_a_count + latest_b_count;
		post_drain_cb(NULL);
		assert(reposts + latest_a_count + latest_b_count - before <= POST_DRAIN_MAX);
		assert(++passes < 20000);
	}
	assert(passes >= 10200 / POST_DRAIN_MAX);
	post_drain_cb(NULL);
	assert(!ringing());

	assert(gui_post(slow, NULL));
	assert(gui_post(slow, NULL));
	post_drain_cb(NULL);
	assert(slow_count == 1 && post_count == 1 && ringing());
	post_drain_cb(NULL);
	assert(slow_count == 2 && post_count == 0);
	post_drain_cb(NULL);
	assert(!ringing());
	close(post_wake_fd);
	post_wake_fd = -1;
	assert(gui_post(repost, NULL));
	post_drain_cb(NULL); // No descriptor: retained work still drains on timer.
	assert(post_count == 0);
	puts("PASS bounded GUI drain: FIFO, coalescing, refusal, round-robin fairness, 10000 reposts, elapsed cutoff, rearm and idle");
}
