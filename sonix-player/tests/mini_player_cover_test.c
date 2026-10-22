// Actual player mini-only demand functions, with inert shared cover backends.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/gui/nowplaying/cover.h"
#include "src/gui/shell/sx_tokens.h"
#include "src/system/library/covertone.h"
static bool compact, tone_known, have_thumb;
static unsigned thumb_reads, scaled_copies, tone_requests, releases;
static int64_t np_album_id, mini_thumb_album;
static uint8_t mini_thumb_buf[COVERTONE_THUMB_BYTES(COVERTONE_THUMB_SMALL)];
static lv_image_dsc_t mini_thumb_dsc;
static uint32_t mini_thumb_generation;
static cover_image_t mini_cover_copy, current_cover;
static const lv_image_dsc_t *bound;
bool sx_mini_wants_cover(void) { return !compact; }
void sx_mini_set_cover(const lv_image_dsc_t *source, uint32_t generation) { (void)generation; if (!compact || !source) bound = source; }
void sx_artview_release_cover(cover_image_t *cover) {
	assert(!bound || bound != &cover->dsc);
	assert(cover != &current_cover);
	free(cover->pixels); memset(cover, 0, sizeof(*cover)); releases++;
}
bool covertone_for_album(int64_t id, float *h, float *c, float *l) { (void)id; *h = 31; *c = .15; *l = .6; return tone_known; }
void covertone_request(int64_t id) { assert(id == np_album_id && id != 0); tone_requests++; }
bool covertone_thumb(int64_t id, int size, lv_image_dsc_t *out, void *pixels, size_t bytes) {
	(void)id; (void)size; (void)pixels; (void)bytes; thumb_reads++; memset(out, 0, sizeof(*out)); return have_thumb;
}
uint32_t cover_new_display_generation(void) { return 1; }
bool cover_scaled_copy(const cover_image_t *source, int size, cover_image_t *out) {
	(void)size; assert(source == &current_cover); scaled_copies++; out->pixels = malloc(8); assert(out->pixels); return true;
}
#include "actual_cover_demand.inc"
int main(void) {
	static uint8_t original[8]; current_cover.pixels = original;
	np_album_id = 8; compact = true;
	np_mini_thumb_refresh(); assert(tone_requests == 1 && thumb_reads == 0 && scaled_copies == 0);
	tone_known = true; np_mini_thumb_refresh(); assert(tone_requests == 1 && thumb_reads == 0);
	np_album_id = 0; np_mini_from_cover(); assert(scaled_copies == 0);
	compact = false; np_mini_from_cover(); assert(scaled_copies == 1 && bound == &mini_cover_copy.dsc);
	compact = true; player_mini_mode_changed(); assert(!bound && !mini_cover_copy.pixels);
	assert(current_cover.pixels == original);
	compact = false; player_mini_mode_changed(); assert(scaled_copies == 2 && bound == &mini_cover_copy.dsc);
	compact = true; player_mini_mode_changed(); assert(!bound && !mini_cover_copy.pixels);
	np_album_id = 8; have_thumb = true; compact = false;
	np_mini_thumb_refresh(); assert(thumb_reads == 1 && bound == &mini_thumb_dsc && mini_thumb_album == 8);
	compact = true; player_mini_mode_changed(); assert(!bound && mini_thumb_album == 0);
	assert(thumb_reads == 1 && scaled_copies == 2 && current_cover.pixels == original && releases >= 4);
	puts("PASS actual mini-only demand: hidden thumbnails/copies skipped, shared cover colour request retained, detach before free, normal restore");
}
