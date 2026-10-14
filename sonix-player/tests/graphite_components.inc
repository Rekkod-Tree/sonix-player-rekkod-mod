// Included by the isolated native UI runner; never linked into target builds.
#include "src/gui/nowplaying/trackmenu.h"
#include "src/gui/settings/timeset.h"
#include "src/system/bluetooth/bluetooth.h"
#include "src/system/device/clock.h"
#include "src/system/streaming/zvukcache.h"
#include "src/system/streaming/zvukdl.h"
#include <sys/stat.h>

static lv_obj_t *visible_label(lv_obj_t *root, const char *text) {
	if (lv_obj_is_hidden(root))
		return NULL;
	if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = visible_label(lv_obj_get_child(root, i), text);
		if (found)
			return found;
	}
	return NULL;
}

static lv_obj_t *test_switch(lv_obj_t *root) {
	if (lv_obj_check_type(root, &lv_switch_class))
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = test_switch(lv_obj_get_child(root, i));
		if (found)
			return found;
	}
	return NULL;
}

static void component_states(void) {
	static bool battery_before;
	static int timezone_before;
	int step = phase++;
	int round = step / 16;
	if (round == 2) {
		printf("UI_TEST_PASS filled choices, note hierarchy/details, date/time cancellation and 16 quick controls\n");
		logging_flush();
		exit(0);
	}
	char name[64];
	snprintf(name, sizeof(name), "components-%s-%d", round ? "light-large" : "dark", step % 16);
	switch (step % 16) {
	case 0:
		if (!step) {
			gui_init_rest();
			pointer = lv_indev_create();
			lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
			lv_indev_set_read_cb(pointer, read_pointer);
		}
		theme_set_dark(!round);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		fonts_set_large_text(round != 0);
		switch_screen(appearance_screen);
		break;
	case 1: {
		capture(name);
		lv_obj_t *selected = lv_obj_get_parent(visible_label(appearance_screen, tr(round ? "appearance_light" : "appearance_dark")));
		assert(lv_obj_has_state(selected, LV_STATE_CHECKED));
		assert(lv_obj_get_style_bg_opa(selected, 0) == LV_OPA_COVER);
		assert(lv_obj_get_child_count(selected) == 1);
		assert(lv_obj_get_height(selected) == 80);
		lv_obj_t *other = lv_obj_get_parent(visible_label(appearance_screen, tr(round ? "appearance_dark" : "appearance_light")));
		assert(!lv_obj_has_state(other, LV_STATE_CHECKED));
		lv_obj_send_event(other, LV_EVENT_CLICKED, NULL);
		break;
	}
	case 2:
		assert(theme_is_dark() == (round != 0));
		capture(name);
		theme_set_dark(!round);
		break;
	case 3:
		switch_screen(btaudio_screen);
		break;
	case 4: {
		lv_obj_t *title = visible_label(btaudio_screen, tr("btaudio_headset_battery"));
		assert(title);
		lv_obj_t *head = lv_obj_get_parent(lv_obj_get_parent(title));
		lv_obj_t *card = lv_obj_get_parent(head);
		lv_obj_scroll_to_view(card, LV_ANIM_OFF);
		lv_obj_t *sw = test_switch(head);
		assert(sw);
		lv_obj_t *summary = visible_label(card, tr("btaudio_headset_battery_summary"));
		assert(summary && lv_obj_get_parent(summary) == card);
		battery_before = bluetooth_headset_battery_enabled();
		capture(name);
		lv_obj_t *details = visible_label(card, tr("details"));
		assert(details);
		lv_obj_send_event(lv_obj_get_parent(details), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 5:
		assert(sx_sheet_is_open());
		assert(bluetooth_headset_battery_enabled() == battery_before);
		assert(visible_label(lv_layer_top(), tr("btaudio_headset_battery_note")));
		capture(name);
		sx_sheet_close();
		break;
	case 6:
		timezone_before = clock_utc_offset_minutes();
		timeset_show();
		break;
	case 7: {
		lv_obj_t *confirm = visible_label(lv_layer_top(), tr("confirm"));
		assert(confirm);
		lv_obj_t *button = lv_obj_get_parent(confirm);
		lv_area_t box;
		lv_obj_get_coords(button, &box);
		assert(lv_obj_get_width(button) == 448 && lv_obj_get_height(button) == 88);
		assert(box.y2 < lv_display_get_vertical_resolution(lv_display_get_default()));
		assert(lv_obj_get_style_radius(button, 0) == SX_CARD_R);
		capture(name);
		break;
	}
	case 8: {
		lv_obj_t *zone = visible_label(lv_layer_top(), tr("timeset_time_zone"));
		assert(zone);
		lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(zone)));
		lv_obj_send_event(card, LV_EVENT_CLICKED, NULL);
		break;
	}
	case 9:
		assert(visible_label(lv_layer_top(), "UTC-12:00"));
		capture(name);
		input(44, 88, true);
		input(44, 88, false);
		break;
	case 10:
		assert(visible_label(lv_layer_top(), tr("confirm")));
		input(44, 88, true);
		input(44, 88, false);
		assert(clock_utc_offset_minutes() == timezone_before);
		break;
	case 11:
		switch_screen(main_menu_screen);
		for (int i = 0; i < QP_SLOT_COUNT; i++)
			quickpanel_move_in_use((quickpanel_button_t)i, i);
		quickpanel_open();
		break;
	case 12: {
		capture(name);
		lv_obj_t *icon = find_image(lv_layer_top(), &icon_wifi);
		assert(icon);
		lv_obj_t *cell = lv_obj_get_parent(lv_obj_get_parent(icon));
		lv_obj_t *panel = lv_obj_get_parent(lv_obj_get_parent(cell));
		assert(lv_obj_get_height(lv_obj_get_child(cell, 1)) >= 2 * lv_font_get_line_height(&font_ui_20));
		lv_obj_t *handle = lv_obj_get_child(panel, lv_obj_get_child_count(panel) - 1);
		assert(lv_obj_get_width(handle) == 160 && visible(handle));
		lv_obj_send_event(handle, LV_EVENT_CLICKED, NULL);
		break;
	}
	case 13: {
		capture(name);
		lv_obj_t *icon = find_image(lv_layer_top(), &icon_wifi);
		lv_obj_t *panel = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(icon))));
		lv_area_t box;
		lv_obj_get_coords(panel, &box);
		assert(box.y2 < lv_display_get_vertical_resolution(lv_display_get_default()));
		quickpanel_close();
		break;
	}
	case 14:
		assert(!quickpanel_is_open());
		rows_screen = lv_obj_create(NULL);
		lv_obj_add_style(rows_screen, &sx_style_page, 0);
		for (int i = 0; i < 4; i++) {
			lv_obj_t *item = row(140 + i * 100, "A long track title that must remain on one line", NULL);
			sx_row_set_subtitle(item, "A long artist name", i % 2 ? "FLAC" : NULL, "1:23:45");
			sx_row_set_compact(item, i >= 2);
			if (i == 3)
				sx_row_set_trail(item, SX_ROW_TRAIL_GRIP);
		}
		lv_screen_load(rows_screen);
		break;
	case 15: {
		capture(name);
		lv_obj_t *measure = lv_label_create(lv_layer_top());
		lv_obj_set_style_text_font(measure, &font_ui_20, 0);
		lv_label_set_text(measure, "1:23:45");
		lv_obj_update_layout(measure);
		int duration_width = lv_obj_get_width(measure);
		lv_obj_delete(measure);
		for (uint32_t i = 0; i < lv_obj_get_child_count(rows_screen); i++) {
			lv_obj_t *item = lv_obj_get_child(rows_screen, i);
			lv_obj_t *duration = visible_label(item, "1:23:45");
			assert(duration);
			lv_area_t bounds, text;
			lv_obj_get_coords(item, &bounds);
			lv_obj_get_coords(duration, &text);
			assert(bounds.x2 - text.x2 >= 12);
			assert(lv_obj_get_width(duration) >= duration_width);
			lv_area_t line;
			lv_obj_get_coords(lv_obj_get_parent(duration), &line);
			assert(text.x2 <= line.x2 && text.x1 >= line.x1);
		}
		lv_screen_load(main_menu_screen);
		lv_obj_delete(rows_screen);
		rows_screen = NULL;
		break;
	}
	}
}

static char save_fixture_paths[2][512];
static void save_fixture(int index) {
	zvuk_track_t track = {0};
	snprintf(track.id, sizeof(track.id), "%d", 201 + index);
	snprintf(track.album_id, sizeof(track.album_id), "701");
	snprintf(track.title, sizeof(track.title), "Save audit %d", index + 1);
	snprintf(track.artist, sizeof(track.artist), "Test artist");
	snprintf(track.album, sizeof(track.album), "Test album");
	track.available = true;
	track.track_number = index + 1;
	zvukcache_path(track.id, "flac", save_fixture_paths[index], sizeof(save_fixture_paths[index]));
	const char *dir = zvukcache_dir();
	assert(dir && (mkdir(dir, 0700) == 0 || errno == EEXIST));
	FILE *src = fopen(getenv("SONIX_UI_TEST_TRACK"), "rb"), *dst = fopen(save_fixture_paths[index], "wb");
	assert(src && dst);
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
		assert(fwrite(buf, 1, n, dst) == n);
	fclose(src);
	assert(fclose(dst) == 0);
	zvukcache_write_sidecars(&track, save_fixture_paths[index]);
}

static void save_menu_states(void) {
	switch (phase++) {
	case 0:
		gui_init_rest();
		theme_set_dark(true);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		mock_sign_in(true);
		save_fixture(0);
		save_fixture(1);
		player_play_file(save_fixture_paths[0]);
		player_sheet_open(false);
		break;
	case 1:
		trackmenu_open(NULL);
		break;
	case 2: {
		lv_obj_t *save = visible_label(lv_layer_top(), tr("zvuk_save"));
		assert(save);
		capture("stream-menu-save");
		// A track change while the sheet is open must not change its target.
		player_play_file(save_fixture_paths[1]);
		lv_obj_send_event(lv_obj_get_parent(save), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 3:
	case 4:
	case 5:
	case 6:
		break;
	case 7: {
		char path[1024];
		assert(zvukdl_local("201", path, sizeof(path)));
		assert(!zvukdl_saved("202"));
		player_play_file(save_fixture_paths[0]);
		break;
	}
	case 8:
		trackmenu_open(NULL);
		break;
	case 9:
		assert(visible_label(lv_layer_top(), tr("zvuk_saved_on_card")));
		assert(!visible_label(lv_layer_top(), tr("zvuk_save")));
		capture("stream-menu-already-saved");
		sx_sheet_close();
		player_play_file(save_fixture_paths[1]);
		zvukdl_set_root(NULL);
		break;
	case 10:
		trackmenu_open(NULL);
		break;
	case 11: {
		lv_obj_t *save = visible_label(lv_layer_top(), tr("zvuk_save"));
		assert(save);
		lv_obj_send_event(lv_obj_get_parent(save), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 12:
		assert(visible_label(lv_layer_top(), tr("zvuk_save_refused")));
		capture("stream-menu-no-card");
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		break;
	case 13:
		trackmenu_open(NULL);
		break;
	case 14:
		assert(!visible_label(lv_layer_top(), tr("zvuk_save")));
		printf("UI_TEST_PASS playing-stream save, original-track snapshot, already-saved state, missing card and local-track exclusion\n");
		logging_flush();
		exit(0);
	}
}
