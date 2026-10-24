// Production demand and ownership; asynchronous transport and UI are inert.
#include "src/gui/nowplaying/cover.h"
#include "src/gui/shell/sx_tokens.h"
#include "src/system/library/covertone.h"
#include "src/system/playback/device_state.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool compact, tone_known, have_thumb, visible = true;
static unsigned thumb_reads, async_requests, scaled_copies, cached_copies, tone_requests, releases, frees;
static int64_t np_album_id, mini_thumb_album, mini_large_attempted;
static bool np_album_tone_known;
static uint8_t mini_thumb_buf[COVERTONE_THUMB_BYTES(COVERTONE_THUMB_SMALL)];
static lv_image_dsc_t mini_thumb_dsc, thumb240_dsc;
static uint32_t mini_thumb_generation, thumb240_ticket;
static bool thumb240_held, cover_request_outstanding, layout_studio_now;
static int64_t thumb240_album;
static lv_obj_t *cover_thumb_img, *cover_placeholder_icon;
static int cover_id_state;
enum { COVER_ID_IDLE = 0 };
static char cover_shown_path[512], current_path[512];
static cover_image_t mini_cover_copy, current_cover;
static const lv_image_dsc_t *bound;
static covertone_thumb_cb pending_cb;
static int64_t pending_album;
static uint32_t pending_ticket, ticket_serial;
static void np_thumb240_request(void);
static void np_thumb240_drop(void);
bool sx_mini_wants_cover(void) { return visible && !compact; }
void sx_mini_set_cover(const lv_image_dsc_t *src, uint32_t generation) {
	(void)generation;
	if (!compact || !src)
		bound = src;
}
void sx_artview_release_cover(cover_image_t *c) {
	assert(!bound || bound != &c->dsc);
	assert(c != &current_cover);
	free(c->pixels);
	memset(c, 0, sizeof(*c));
	releases++;
}
bool covertone_for_album(int64_t id, float *h, float *c, float *l) {
	assert(id == np_album_id);
	*h = 31;
	*c = .15;
	*l = .6;
	return tone_known;
}
void covertone_request(int64_t id) {
	assert(id == np_album_id && id);
	tone_requests++;
}
bool covertone_thumb(int64_t id, int size, lv_image_dsc_t *out, void *pixels, size_t bytes) {
	assert(id == np_album_id && size == COVERTONE_THUMB_SMALL); // Real API forbids UI reads of240.
	assert(bytes >= COVERTONE_THUMB_BYTES(size));
	thumb_reads++;
	memset(out, 0, sizeof(*out));
	out->header.w = out->header.h = size;
	out->header.stride = size * 2;
	out->data = pixels;
	return have_thumb;
}
uint32_t covertone_thumb_async(int64_t id, int size, covertone_thumb_cb cb, void *user) {
	assert(id == np_album_id && size == 240 && !user && !pending_cb);
	async_requests++;
	pending_cb = cb;
	pending_album = id;
	pending_ticket = ++ticket_serial;
	return pending_ticket;
}
void covertone_thumb_cancel(uint32_t ticket) {
	if (ticket == pending_ticket) {
		pending_cb = NULL;
		pending_ticket = 0;
	}
}
void covertone_thumb_free(lv_image_dsc_t *dsc) {
	free((void *)dsc->data);
	dsc->data = NULL;
	frees++;
}
uint32_t cover_new_display_generation(void) { return 1; }
bool cover_scaled_copy(const cover_image_t *src, int size, cover_image_t *out) {
	assert(size == 88 && src->pixels);
	if (src == &current_cover)
		scaled_copies++;
	else {
		assert(src->dsc.header.w == 240);
		cached_copies++;
	}
	out->pixels = malloc(COVERTONE_THUMB_BYTES(88));
	assert(out->pixels);
	return true;
}
void device_state_get(device_state_t *s) {
	memset(s, 0, sizeof(*s));
	snprintf(s->current_file, sizeof(s->current_file), "%s", current_path);
}
bool library_track_album_value(const char *path, char *out, size_t size) {
	snprintf(out, size, "%s", path + 1);
	return path[0] == '/';
}
int64_t covertone_album_id(const char *value) { return strtoll(value, NULL, 10); }
void lv_obj_set_hidden(lv_obj_t *o, bool hidden) {
	(void)o;
	(void)hidden;
}
void lv_image_set_src(lv_obj_t *o, const void *src) {
	(void)o;
	(void)src;
}
void sx_artview_detach(lv_obj_t *o) { (void)o; }
void sx_artview_bind(lv_obj_t *o, const lv_image_dsc_t *d, uint32_t g) {
	(void)o;
	(void)d;
	(void)g;
}
void sx_artview_refresh(void) {}
#include "actual_cover_demand.inc"
static lv_image_dsc_t large_image(void) {
	lv_image_dsc_t d = {0};
	d.header.w = d.header.h = 240;
	d.data = malloc(COVERTONE_THUMB_BYTES(240));
	assert(d.data);
	return d;
}
static void deliver(bool success) {
	assert(pending_cb);
	covertone_thumb_cb cb = pending_cb;
	int64_t id = pending_album;
	pending_cb = NULL;
	pending_ticket = 0;
	lv_image_dsc_t d = success ? large_image() : (lv_image_dsc_t){0};
	cb(id, 240, success ? &d : NULL, NULL);
}
int main(void) {
	static uint8_t original[COVERTONE_THUMB_BYTES(240)];
	current_cover.pixels = original;
	np_album_id = 8;
	compact = true;
	np_mini_thumb_refresh();
	assert(tone_requests == 1 && !thumb_reads && !async_requests);
	tone_known = true;
	np_mini_thumb_refresh();
	assert(tone_requests == 1 && !thumb_reads);
	compact = false;
	visible = false;
	np_mini_thumb_refresh();
	assert(!thumb_reads && !async_requests);
	visible = true;
	compact = true;
	np_album_id = 0;
	np_mini_from_cover();
	assert(!scaled_copies);
	compact = false;
	np_mini_from_cover();
	assert(scaled_copies == 1 && bound == &mini_cover_copy.dsc);
	compact = true;
	player_mini_mode_changed();
	assert(!bound && !mini_cover_copy.pixels);
	compact = false;
	player_mini_mode_changed();
	assert(scaled_copies == 2 && bound == &mini_cover_copy.dsc);
	compact = true;
	player_mini_mode_changed();
	assert(!bound && !mini_cover_copy.pixels && current_cover.pixels == original);
	np_album_id = 8;
	have_thumb = true;
	compact = false;
	snprintf(current_path, sizeof(current_path), "/8");
	np_mini_thumb_refresh();
	assert(async_requests == 1 && thumb_reads == 1 && bound == &mini_thumb_dsc);
	np_mini_thumb_refresh();
	assert(async_requests == 1 && thumb_reads == 1);
	deliver(true);
	assert(cached_copies == 1 && frees == 1 && bound == &mini_cover_copy.dsc && mini_thumb_album == 8);
	uint8_t *same = mini_cover_copy.pixels;
	unsigned old_releases = releases;
	np_album_changed("/8", false);
	assert(mini_cover_copy.pixels == same && releases == old_releases && async_requests == 1);
	np_album_changed("/9", false);
	snprintf(current_path, sizeof(current_path), "/9");
	assert(async_requests == 2 && thumb_reads == 2 && bound == &mini_thumb_dsc);
	deliver(true);
	assert(cached_copies == 2 && mini_thumb_album == 9);
	lv_image_dsc_t stale = large_image();
	thumb240_arrived(8, 240, &stale, NULL);
	assert(cached_copies == 2 && mini_thumb_album == 9 && bound == &mini_cover_copy.dsc);
	cover_request_outstanding = true;
	cover_thumb_img = (lv_obj_t *)(uintptr_t)1;
	np_thumb240_request();
	assert(async_requests == 3 && thumb240_ticket);
	compact = true;
	player_mini_mode_changed();
	assert(thumb240_ticket && !bound);
	deliver(true);
	assert(thumb240_held && !mini_cover_copy.pixels);
	cover_request_outstanding = false;
	np_thumb240_drop();
	assert(!thumb240_held);
	snprintf(cover_shown_path, sizeof(cover_shown_path), "/9");
	compact = false;
	player_mini_mode_changed();
	assert(scaled_copies == 3 && mini_thumb_album == 9 && async_requests == 3);
	compact = true;
	player_mini_mode_changed();
	compact = false;
	np_album_id = 10;
	snprintf(current_path, sizeof(current_path), "/10");
	np_mini_from_cover();
	assert(!mini_cover_copy.pixels);
	snprintf(cover_shown_path, sizeof(cover_shown_path), "/10");
	cover_id_state = 1;
	np_mini_from_cover();
	assert(!mini_cover_copy.pixels);
	cover_id_state = 0;
	np_mini_from_cover();
	assert(scaled_copies == 4 && mini_thumb_album == 10);
	have_thumb = false;
	np_album_changed("/11", false);
	assert(async_requests == 4);
	deliver(false);
	np_mini_thumb_refresh();
	assert(async_requests == 4 && !mini_cover_copy.pixels);
	compact = true;
	player_mini_mode_changed();
	np_thumb240_drop();
	assert(current_cover.pixels == original && !pending_cb && !bound);
	puts("PASS shared async mini artwork, no UI240 read, fallback upgrade, once per album, safe replacement, stale delivery, full-cover reuse and preserved full-player request");
}
