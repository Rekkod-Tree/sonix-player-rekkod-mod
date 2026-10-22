// Actual svclist + LVGL layout/virtualization; simple row/frame fixtures.
// The fixture has no service, playback, network, filesystem or page workers.
#include "lvgl/lvgl.h"
#include "src/gui/streaming/svclist.c"
#include <assert.h>
#include <stdio.h>

lv_font_t font_ui_17, font_ui_14;
static int ids[20000], key_calls, bind_calls, visible_first, visible_last;
static bool grouped, large;
static int visible_calls, bind_min, bind_max;

int sx_row_height(bool compact) {
	(void)compact;
	return large ? 120 : 96;
}
lv_obj_t *sx_row_create(lv_obj_t *parent, const sx_row_handlers_t *h, bool compact) {
	(void)h;
	lv_obj_t *row = lv_obj_create(parent);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, 440, sx_row_height(compact));
	return row;
}
void sx_row_set_compact(lv_obj_t *row, bool compact) { lv_obj_set_height(row, sx_row_height(compact)); }
void sx_row_set_user(lv_obj_t *row, void *user) { lv_obj_set_user_data(row, user); }
void *sx_row_get_user(lv_obj_t *row) { return lv_obj_get_user_data(row); }
void sx_frame_reserve_bottom(lv_obj_t *s) { lv_obj_set_style_pad_bottom(s, 128, 0); }
static void bind(lv_obj_t *row, int index) {
	assert(index >= 0 && index < 20000);
	assert(svclist_index_of(row) == index);
	bind_calls++;
	if (index < bind_min)
		bind_min = index;
	if (index > bind_max)
		bind_max = index;
}
static bool key(int index, char *out, size_t size) {
	key_calls++;
	int n = snprintf(out, size, "track:%d", ids[index]);
	return n > 0 && (size_t)n < size;
}
static bool heading(int index, char *out, size_t n) {
	if (!grouped || index % 8)
		return false;
	snprintf(out, n, "Group %d", index / 8);
	return true;
}
static void visible(int first, int last) {
	visible_calls++;
	visible_first = first;
	visible_last = last;
}
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *p) {
	(void)a;
	(void)p;
	lv_display_flush_ready(d);
}
static void move_to(svclist_t *l, int index, int offset) {
	lv_obj_scroll_to_y(l->scroller, entry_y(l, index) + offset, LV_ANIM_OFF);
	lv_obj_update_layout(l->scroller);
}
static void roundtrip(svclist_t *l, const svclist_anchor_t *a, int expected) {
	int keys_before = key_calls;
	svclist_restore(l, a);
	assert(key_calls - keys_before >= 1 && key_calls - keys_before <= l->count + 1);
	if (expected == a->index && atoi(a->key + 6) == ids[expected])
		assert(key_calls - keys_before == 1);
	svclist_anchor_t restored;
	assert(svclist_capture(l, &restored));
	assert(restored.index == expected && restored.offset == a->offset);
	assert(visible_first == expected && visible_last >= visible_first);
}
static void check(svclist_t *l) {
	for (int i = 0; i < 20000; i++)
		ids[i] = i + 100;
	svclist_set_top(l, 72);
	svclist_set_count(l, 20000, false);
	lv_obj_update_layout(l->scroller);
	uint32_t objects = lv_obj_get_child_count(l->scroller);
	svclist_anchor_t a;
	assert(!svclist_capture(NULL, &a) && !a.valid);
	move_to(l, 12034, 23);
	assert(svclist_capture(l, &a));
	assert(a.index == 12034 && a.offset == 23 && a.key[0]);
	svclist_set_count(l, 0, false);
	assert(!svclist_capture(l, &(svclist_anchor_t){0}));
	visible_calls = 0;
	bind_min = 20000;
	bind_max = -1;
	svclist_set_count_at(l, 20000, &a);
	assert(visible_calls == 1 && visible_first == 12034);
	assert(bind_min >= 12034 - 24 / 3 && bind_max <= 12034 + 24);
	roundtrip(l, &a, 12034);
	// The same ID moved while the album was open; restore it, not the old index.
	int saved = ids[12034];
	ids[12034] = -1;
	ids[12042] = saved;
	roundtrip(l, &a, 12042);
	ids[12042] = -2;
	roundtrip(l, &a, 12034); // Missing ID: nearest surviving index.
	ids[12034] = saved;
	large = true;
	svclist_set_count(l, 20000, false);
	roundtrip(l, &a, 12034);
	lv_obj_set_height(l->scroller, 430);
	roundtrip(l, &a, 12034);
	// Header space has a negative row-relative offset and must survive too.
	move_to(l, 0, -52);
	assert(svclist_capture(l, &a) && a.index == 0 && a.offset == -52);
	svclist_set_count(l, 20000, false);
	roundtrip(l, &a, 0);
	// Fresh category is explicitly count(..., false): no retained anchor.
	move_to(l, 15000, 11);
	assert(svclist_capture(l, &a));
	svclist_set_count(l, 20000, false);
	assert(lv_obj_get_scroll_y(l->scroller) == 0);
	// Shorter response and bottom padding: clamp to actual available viewport.
	svclist_set_count(l, 10, false);
	svclist_restore(l, &a);
	assert(lv_obj_get_scroll_bottom(l->scroller) >= 0);
	int max = lv_obj_get_scroll_y(l->scroller) + lv_obj_get_scroll_bottom(l->scroller);
	assert(lv_obj_get_scroll_y(l->scroller) <= max);
	assert(lv_obj_get_child_count(l->scroller) == objects);
	assert(bind_calls < 1000);
}
int main(int argc, char **argv) {
	(void)argc;
	lv_init();
	const char *font_dir = getenv("SONIX_TEST_FONT_DIR");
	assert(font_dir);
	char font[1024];
	snprintf(font, sizeof(font), "%s/ui-regular.ttf", font_dir);
	const lv_font_t *regular = lv_freetype_font_create(font, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 17, LV_FREETYPE_FONT_STYLE_NORMAL);
	assert(regular);
	font_ui_17 = font_ui_14 = *regular;
	lv_display_t *d = lv_display_create(480, atoi(argv[1]));
	static uint16_t frame[480 * 800];
	lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(d, frame, NULL, sizeof(frame), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(d, flush);
	sx_styles_init();
	for (int g = 0; g < 2; g++) {
		grouped = g;
		large = false;
		bind_calls = key_calls = 0;
		svclist_cbs_t cbs = {.bind = bind, .key = key, .heading = g ? heading : NULL, .visible = visible};
		svclist_t *l = svclist_create(lv_screen_active(), NULL, &cbs, 24, false);
		assert(l);
		lv_obj_set_size(l->scroller, 440, atoi(argv[1]) - 180);
		check(l);
		printf("PASS %s grouped=%d 20k stable/missing/moved ID/header/viewport/large-row/pool binds=%d\n", argv[1], g, bind_calls);
		lv_obj_delete(l->scroller);
	}
	return 0;
}
