// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/gui_post_test.c -pthread -Wl,--gc-sections -o /tmp/gui_post_test
#define _GNU_SOURCE
#include "src/gui/shell/gui.c"
#include <assert.h>
bool clock_poll(void) { return false; }
void topbar_refresh_clock(void) {}
static int ordinary, completions, volume, order_count;
static void notice(void *unused) {
	(void)unused;
	assert(completions == 1 && volume == 44);
	ordinary++;
}
static void completion(void *unused) {
	(void)unused;
	assert(order_count++ == 0);
	completions++;
}
static void level(void *value) {
	assert(order_count++ == 1);
	volume = (int)(intptr_t)value;
}
int main(void) {
	for (int i = 0; i < GUI_POST_MAX; i++)
		assert(gui_post(notice, NULL));
	assert(!gui_post(notice, NULL));
	assert(!gui_post_or_free(notice, malloc(128)));
	assert(gui_post_latest(completion, NULL));
	assert(gui_post_latest(completion, NULL));
	assert(gui_post_latest(level, (void *)(intptr_t)35));
	assert(gui_post_latest(level, (void *)(intptr_t)44));
	post_drain_cb(NULL);
	assert(ordinary == 32 && completions == 1 && volume == 44 && post_count == 0);
	puts("PASS full bounded queue: owned refusal freed, completion retained once, volume coalesced");
}
