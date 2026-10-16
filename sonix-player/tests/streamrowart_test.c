// Real LVGL rows/deletion with deterministic normal HTTP/image fixtures.
// Include the production helper; no network, device or player thread is used.
#include <assert.h>
#include <pthread.h>
static int test_thread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
#define pthread_create test_thread_create
#include "src/gui/streaming/streamrowart.c"
#undef pthread_create
#include "lvgl/src/misc/lv_timer_private.h"

typedef struct { lv_obj_t *image; } fixture_row_t;
static int requests, decodes;
static bool network_ok = true;
lv_font_t font_ui_14 = {.line_height = 14}; // no text is drawn by this fixture

static int test_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *u) {
	(void)t; (void)a; (void)fn; (void)u;
	return 0; // complete() runs the worker's real load function deterministically.
}
void thread_be_low_priority(const char *name) { (void)name; }
void http_set_thread_timeout_cap(int seconds) { assert(seconds == FETCH_SECONDS); }
bool http_get(const char *url, char **out, size_t *size, size_t cap, int timeout) {
	assert(strncmp(url, "https://fixture/", 16) == 0);
	assert(cap == 256 * 1024 && timeout == 5);
	requests++;
	*out = network_ok ? strdup("normal-image") : NULL;
	*size = network_ok ? strlen(*out) : 0;
	return network_ok;
}
bool cover_load_image_memory(const void *data, size_t size, int w, int h, cover_fit_t fit, cover_image_t *out) {
	assert(data && size && w == 56 && h == 56 && fit == COVER_FIT_COVER);
	decodes++;
	out->pixels = malloc(56 * 56 * 2);
	assert(out->pixels);
	memset(out->pixels, 0x55, 56 * 56 * 2);
	out->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
	out->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
	out->dsc.header.w = out->dsc.header.h = 56;
	out->dsc.header.stride = 112;
	out->dsc.data_size = 56 * 56 * 2;
	out->dsc.data = out->pixels;
	return true;
}
void cover_free(cover_image_t *image) {
	free(image->pixels);
	memset(image, 0, sizeof(*image));
}
void sx_row_set_thumb(lv_obj_t *row, const lv_image_dsc_t *image, lv_color_t tone) {
	(void)tone;
	fixture_row_t *state = lv_obj_get_user_data(row);
	assert(state);
	lv_image_set_src(state->image, image);
}
static void row_deleted(lv_event_t *e) {
	// Like sx_row's earlier DELETE callback: private state is freed first.
	free(lv_obj_get_user_data(lv_event_get_target(e)));
}
static lv_obj_t *new_row(lv_obj_t *parent, int y) {
	lv_obj_t *row = lv_obj_create(parent);
	lv_obj_set_size(row, 440, 70);
	lv_obj_set_pos(row, 0, y);
	fixture_row_t *state = calloc(1, sizeof(*state));
	state->image = lv_image_create(row);
	lv_obj_set_user_data(row, state);
	lv_obj_add_event_cb(row, row_deleted, LV_EVENT_DELETE, NULL);
	return row;
}
static art_slot_t *slot_of(lv_obj_t *row) {
	for (int i = 0; i < ROW_SLOTS; i++)
		if (slots[i].row == row)
			return &slots[i];
	assert(false);
	return NULL;
}
static void complete(void) {
	assert(busy && requested && !done);
	requested = false;
	load_image(job_url, &result);
	done = true;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
	(void)area; (void)pixels;
	lv_display_flush_ready(display);
}
int main(void) {
	lv_init();
	lv_display_t *display = lv_display_create(480, 800);
	static uint8_t draw[480 * 40 * 2];
	lv_display_set_buffers(display, draw, NULL, sizeof(draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, flush);
	lv_obj_t *owner = lv_obj_create(lv_screen_active());
	lv_obj_set_size(owner, 480, 650);
	lv_obj_t *a = new_row(owner, 0), *b = new_row(owner, 90);
	lv_obj_update_layout(owner);
	lv_color_t tone = lv_color_hex(0x445566);
	streamrowart_bind(owner, a, 0, "https://fixture/album", tone);
	streamrowart_bind(owner, b, 1, "https://fixture/album", tone);
	streamrowart_visible(owner, 0, 1);
	tick(NULL); complete(); tick(NULL);
	assert(requests == 1 && decodes == 1 && slot_of(a)->image.pixels && slot_of(b)->image.pixels);
	assert(slot_of(a)->image.pixels != slot_of(b)->image.pixels && timer->paused);
	// Rebind does not lose a ready picture or repeat its request.
	streamrowart_bind(owner, a, 0, "https://fixture/album", tone);
	assert(slot_of(a)->image.pixels);
	// A recycled row must not receive the old URL's late image.
	streamrowart_bind(owner, a, 2, "https://fixture/old", tone);
	streamrowart_visible(owner, 0, 2); tick(NULL);
	streamrowart_bind(owner, a, 3, "https://fixture/new", tone);
	streamrowart_visible(owner, 0, 3); complete(); tick(NULL);
	assert(!slot_of(a)->image.pixels && strcmp(job_url, "https://fixture/new") == 0);
	// Actual LVGL deletion during a pending request, with private row data freed first.
	lv_obj_delete(a); complete(); tick(NULL);
	assert(timer->paused);
	// Failure settles; explicit new page permits retry, without a polling storm.
	streamrowart_bind(owner, b, 1, "https://fixture/offline", tone);
	streamrowart_visible(owner, 1, 1); tick(NULL);
	network_ok = false; complete(); tick(NULL);
	int before = requests;
	tick(NULL); tick(NULL);
	assert(requests == before && timer->paused && !slot_of(b)->image.pixels);
	streamrowart_reset(owner);
	streamrowart_bind(owner, b, 1, "https://fixture/offline", tone);
	streamrowart_visible(owner, 1, 1); tick(NULL);
	network_ok = true; complete(); tick(NULL);
	assert(slot_of(b)->image.pixels);
	// Hidden rows release pixels, never fetch; deletion with an attached image is safe.
	lv_obj_set_hidden(b, true); streamrowart_visible(owner, 1, 1); tick(NULL);
	assert(!slot_of(b)->image.pixels && timer->paused);
	lv_obj_set_hidden(b, false); streamrowart_visible(owner, 1, 1); tick(NULL); complete(); tick(NULL);
	assert(slot_of(b)->image.pixels);
	lv_obj_delete(owner);
	lv_timer_handler();
	lv_timer_delete(timer);
	lv_display_delete(display);
	lv_deinit();
	puts("PASS: visible-only, same-URL fanout, recycled identity, late delete, failure/retry, detach/free, idle timer");
	return 0;
}
