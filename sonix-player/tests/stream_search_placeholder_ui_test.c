// Actual cached streaming search fields: palette contrast and legacy return.
// See UI_NAVIGATION_2026-10-21.md at the repository root for the host command.
#define graphite_ui_test_init placeholder_original_init
#include "graphite_ui_test.c"
#undef graphite_ui_test_init
#include "lvgl/src/display/lv_display_private.h"
#include "src/gui/streaming/radiopage.h"
#include "src/system/core/logging.h"
#include <math.h>
static lv_obj_t *hint_screen[7], *hint_field[7];
static unsigned hint_deleted, hint_checks;
static lv_color_t hint_legacy[2][7];
static lv_opa_t hint_legacy_opa[2][7];
static lv_area_t hint_geometry[7];
static const lv_font_t *hint_font[7];
static int hint_caret_width[7];
static int hint_dark, hint_scene;
static const char *hint_ids[] = {"yandex", "zvuk", "tidal", "qobuz", "subsonic", "radio", "podcast"};
static const char *hint_titles[] = {"yandex", "zvuk", "tidal_search_tidal", "qobuz_search_qobuz", "subsonic_search", "radio_search_radio", "podcast_search_for_a_podcast"};
static void hint_delete(lv_event_t *e) {
	(void)e;
	hint_deleted++;
}
static lv_obj_t *hint_label(lv_obj_t *obj, const char *text) {
	if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text))
		return obj;
	for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
		lv_obj_t *hit = hint_label(lv_obj_get_child(obj, (int)i), text);
		if (hit)
			return hit;
	}
	return NULL;
}
static double hint_channel(unsigned n) {
	double c = n / 255.;
	return c <= .04045 ? c / 12.92 : pow((c + .055) / 1.055, 2.4);
}
static double hint_lum(lv_color_t c) { return .2126 * hint_channel(c.red) + .7152 * hint_channel(c.green) + .0722 * hint_channel(c.blue); }
static double hint_ratio(lv_color_t ink, lv_color_t bg, lv_opa_t opa) {
	ink = lv_color_make((ink.red * opa + bg.red * (255 - opa) + 127) / 255, (ink.green * opa + bg.green * (255 - opa) + 127) / 255, (ink.blue * opa + bg.blue * (255 - opa) + 127) / 255);
	double a = hint_lum(ink), b = hint_lum(bg);
	return a > b ? (a + .05) / (b + .05) : (b + .05) / (a + .05);
}
static void hint_find(void) {
	lv_display_t *disp = lv_display_get_default();
	for (int k = 0; k < 7; k++) {
		if (k == 5) {
			hint_screen[k] = radiosearch_screen;
			hint_field[k] = find_field(radiosearch_screen);
		} else
			for (uint32_t i = 0; i < disp->screen_cnt; i++) {
				lv_obj_t *screen = disp->screens[i], *field = find_field(screen);
				if (field && hint_label(screen, tr(hint_titles[k]))) {
					assert(!hint_screen[k]);
					hint_screen[k] = screen;
					hint_field[k] = field;
				}
			}
		fprintf(stderr, "HINT_FIND %s screens=%u found=%p expected=[%s]\n", hint_ids[k], disp->screen_cnt, (void *)hint_screen[k], tr(hint_titles[k]));
		assert(hint_screen[k] && hint_field[k]);
		lv_obj_add_event_cb(hint_field[k], hint_delete, LV_EVENT_DELETE, NULL);
	}
}
static void hint_check(int i, const char *name, bool capture_it) {
	lv_obj_t *field = hint_field[i];
	switch_screen_no_history(hint_screen[i]);
	lv_obj_update_layout(hint_screen[i]);
	keyboard_refresh_caret(field);
	assert(!hint_deleted);
	assert(find_field(hint_screen[i]) == field);
	assert(!strcmp(lv_textarea_get_text(field), ""));
	lv_area_t box;
	lv_obj_get_coords(field, &box);
	if (!hint_checks) {
		for (int k = 0; k < 7; k++) {
			hint_font[k] = lv_obj_get_style_text_font(hint_field[k], 0);
			hint_caret_width[k] = lv_obj_get_style_border_width(hint_field[k], LV_PART_CURSOR);
		}
	}
	assert(lv_obj_get_style_text_font(field, 0) == hint_font[i]);
	assert(lv_obj_get_style_border_width(field, LV_PART_CURSOR) == hint_caret_width[i]);
	assert(lv_obj_get_style_anim_duration(field, LV_PART_CURSOR) == 900);
	if (hint_scene == 0)
		hint_geometry[i] = box;
	else
		assert(!memcmp(&box, &hint_geometry[i], sizeof(box)));
	lv_color_t ink = lv_obj_get_style_text_color(field, LV_PART_TEXTAREA_PLACEHOLDER), bg = lv_obj_get_style_bg_color(field, 0);
	lv_opa_t opa = lv_obj_get_style_text_opa(field, LV_PART_TEXTAREA_PLACEHOLDER);
	double ratio = hint_ratio(ink, bg, opa);
	if (!theme_get_palette()) {
		if (hint_scene == 0) {
			hint_legacy[hint_dark][i] = ink;
			hint_legacy_opa[hint_dark][i] = opa;
		} else {
			assert(lv_color_eq(ink, hint_legacy[hint_dark][i]));
			assert(opa == hint_legacy_opa[hint_dark][i]);
		}
	} else {
		assert(lv_color_eq(ink, sx_pal()->text2));
		assert(ratio >= 4.45);
	}
	printf("HINT_CHECK service=%s family=%d dark=%d ink=%08x bg=%08x opacity=%u contrast=%.3f geometry=%d,%d,%d,%d\n", hint_ids[i], theme_get_palette(), hint_dark, lv_color_to_u32(ink), lv_color_to_u32(bg), opa, ratio, box.x1, box.y1, box.x2, box.y2);
	hint_checks++;
	if (capture_it) {
		char file[128];
		snprintf(file, sizeof(file), "hint-%s-%s-%s", hint_ids[i], hint_dark ? "dark" : "light", name);
		capture(file);
	}
}
static void hint_run(lv_timer_t *timer) {
	static bool initialized;
	if (!initialized) {
		gui_init_rest();
		gui_page_ensure(radiopage_screen);
		hint_find();
		theme_set_dynamic_tint(false);
		theme_set_accent(0);
		initialized = true;
	}
	theme_set_dark(hint_dark);
	int family = hint_scene == 0 ? 0 : hint_scene == 1 ? 7 : hint_scene == 2 ? 0 : hint_scene - 2;
	theme_set_palette(family);
	for (int i = 0; i < 7; i++)
		hint_check(i, hint_scene == 0 ? "legacy-before" : hint_scene == 1 ? "2077" : hint_scene == 2 ? "legacy-return" : "matrix", hint_scene < 3);
	if (hint_scene == 3) {
		// The opt-in style must not alter typed text, cursor position or its main role.
		lv_obj_t *field = hint_field[0];
		switch_screen_no_history(hint_screen[0]);
		lv_textarea_set_text(field, "Kept query");
		lv_textarea_set_cursor_pos(field, 4);
		theme_set_palette(8);
		assert(!strcmp(lv_textarea_get_text(field), "Kept query"));
		assert(lv_textarea_get_cursor_pos(field) == 4);
		assert(lv_color_eq(lv_obj_get_style_text_color(field, 0), sx_pal()->text));
		lv_textarea_set_text(field, "");
	}
	if (++hint_scene <= 10)
		return;
	if (!hint_dark) {
		hint_dark = 1;
		hint_scene = 0;
		return;
	}
	printf("UI_TEST_PASS streaming placeholder actual pages7 themes2 checks=%u geometry_font_caret_legacy_preserved\n", hint_checks);
	lv_timer_delete(timer);
	fflush(stdout);
	logging_flush();
	exit(0);
}
void graphite_ui_test_init(void) {
	signal(SIGABRT, SIG_DFL);
	out_dir = getenv("SONIX_UI_TEST_DIR");
	assert(out_dir);
	lv_timer_create(hint_run, 180, NULL);
}
