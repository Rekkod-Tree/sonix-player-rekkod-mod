// Full LVGL regression; run only through run_graphite_ui.sh's disposable roots.
#include "src/gui/shell/sx_card.h"
static lv_obj_t *polish_screen, *polish_group, *polish_card, *polish_name, *polish_value;

static void polish_mini_bounds(void) {
	lv_obj_t *slot = sx_mini_obj();
	assert(slot && lv_obj_is_visible(slot));
	lv_obj_t *card = lv_obj_get_child(slot, 0);
	lv_obj_update_layout(lv_layer_top());
	lv_area_t bounds;
	lv_obj_get_coords(card, &bounds);
	lv_obj_t *track = NULL;
	for (uint32_t i = 0; i < lv_obj_get_child_count(card); i++) {
		lv_obj_t *child = lv_obj_get_child(card, (int)i);
		if (lv_obj_get_height(child) == 3)
			track = child;
	}
	assert(track);
	lv_area_t line;
	lv_obj_get_coords(track, &line);
	assert(line.x1 >= bounds.x1 + 18 && line.x2 <= bounds.x2 - 18);
	assert(line.y2 <= bounds.y2 - 4);
	for (int position = 0; position <= 100; position += 50) {
		sx_mini_set_progress(position, 100);
		lv_obj_update_layout(lv_layer_top());
		lv_obj_t *fill = lv_obj_get_child(track, 0);
		assert(lv_obj_get_width(fill) <= lv_obj_get_width(track));
		assert(lv_obj_is_hidden(fill) == (position == 0));
	}
}

static void release_polish_states(void) {
	static int step, waits, variant;
	switch (step++) {
	case 0:
		gui_init_rest();
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1:
		if (library_scan_running()) { assert(++waits < 40); step--; break; }
		medialist_set_album_head(true);
		theme_set_palette(0);
		theme_set_dark(true);
		theme_set_dynamic_tint(true);
		open_fixture_album(getenv("SONIX_UI_TEST_TRACK"), "Album");
		break;
	case 2:
		assert(lv_obj_get_style_bg_image_src(medialist_tracks_screen, 0));
		capture("polish-legacy-gradient");
		theme_set_palette(4);
		break;
	case 3:
		assert(!lv_obj_get_style_bg_image_src(medialist_tracks_screen, 0));
		capture("polish-terminal-flat-album");
		theme_set_palette(0);
		break;
	case 4:
		assert(lv_obj_get_style_bg_image_src(medialist_tracks_screen, 0));
		theme_set_dynamic_tint(false);
		break;
	case 5:
		assert(!lv_obj_get_style_bg_image_src(medialist_tracks_screen, 0));
		polish_screen = lv_obj_create(NULL);
		lv_obj_remove_style_all(polish_screen);
		lv_obj_add_style(polish_screen, &sx_style_page, 0);
		polish_group = sx_card_nav_group(polish_screen);
		lv_obj_set_width(polish_group, 448);
		lv_obj_set_pos(polish_group, 16, 150);
		polish_card = sx_card_create(polish_group);
		polish_name = sx_card_title(polish_card, "Внешний вид");
		polish_value = sx_card_value(polish_card, "Тёмная");
		sx_card_chevron(polish_card);
		lv_screen_load(polish_screen);
		sx_mini_set_track("Progress bounds", "/fixture.flac", NULL, false);
		sx_mini_player_shown(false);
		break;
	case 6: {
		int family = variant / 2;
		theme_set_palette(family);
		theme_set_dark((variant % 2) != 0);
		break;
	}
	case 7: {
		int family = variant / 2;
		lv_obj_update_layout(polish_screen);
		lv_area_t name, value;
		lv_obj_get_coords(polish_name, &name);
		lv_obj_get_coords(polish_value, &value);
		assert(name.x2 + 10 <= value.x1);
		// Check painted pixels after opaque row children, not just a style value.
		lv_refr_now(NULL);
		int width, height;
		uint16_t *pixels = display_capture_frame(&width, &height);
		assert(pixels && width == 480 && height >= 720);
		const sx_palette_t *pal = sx_pal();
		lv_color_t edge = family == 4 || family == 5 ? sx_mix(pal->panel_text2, pal->panel, 60) : pal->panel;
		uint16_t expected = (uint16_t)(((edge.red >> 3) << 11) | ((edge.green >> 2) << 5) | (edge.blue >> 3));
		assert(pixels[180 * width + 16] == expected);
		if (family == 4 || family == 5) {
			lv_obj_add_state(polish_card, LV_STATE_PRESSED);
			lv_refr_now(NULL);
			pixels = display_capture_frame(&width, &height);
			assert(pixels[180 * width + 16] == expected);
			lv_obj_remove_state(polish_card, LV_STATE_PRESSED);
		}
		polish_mini_bounds();
		char label[64];
		snprintf(label, sizeof(label), "polish-family-%d-%s", family, variant % 2 ? "dark" : "light");
		capture(label);
		if (++variant < 18) step = 6;
		else { puts("UI_TEST_PASS release polish: flat/gradient lifecycle, terminal frames, title/value gap, mini bounds, nine families light/dark"); fflush(stdout); exit(0); }
		break;
	}
	}
}
