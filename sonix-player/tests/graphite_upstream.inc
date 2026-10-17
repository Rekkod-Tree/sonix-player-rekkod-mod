// Native UI integration checks. All paths and settings must be disposable.
#include "src/gui/audio/metronomepage.h"
#include "src/gui/audio/peqpage.h"
#include "src/gui/library/imageview.h"
#include "src/gui/nowplaying/historypage.h"
#include "src/gui/nowplaying/lyricsearch.h"
#include "src/gui/nowplaying/trackmenu.h"
#include "src/gui/settings/recoverywizard.h"
#include "src/gui/shell/sx_frame.h"
#include "src/system/audio/eq.h"
#include "src/system/library/library.h"

static lv_obj_t *upstream_find(lv_obj_t *root, const char *text) {
	if (lv_obj_is_hidden(root)) return NULL;
	if (lv_obj_check_type(root, &lv_label_class) && strncmp(lv_label_get_text(root), text, strlen(text)) == 0) return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = upstream_find(lv_obj_get_child(root, i), text);
		if (found) return found;
	}
	return NULL;
}

static void upstream_click(lv_obj_t *label) {
	assert(label && visible(label));
	lv_obj_t *target = label;
	while (target && !lv_obj_is_clickable(target))
		target = lv_obj_get_parent(target);
	assert(target);
	lv_obj_scroll_to_view_recursive(target, LV_ANIM_OFF);
	lv_obj_update_layout(lv_screen_active());
	lv_area_t box;
	lv_obj_get_coords(target, &box);
	// scroll_to_view sees the full viewport; the persistent mini overlays its
	// bottom. Scroll into the usable body as a person would, before tapping.
	int bottom = lv_display_get_vertical_resolution(lv_display_get_default()) - sx_frame_body_bottom() - 16;
	if (lv_obj_get_screen(target) == lv_screen_active() && box.y2 > bottom) {
		lv_obj_t *scroller = lv_obj_get_parent(target);
		while (scroller && !lv_obj_is_scrollable(scroller)) scroller = lv_obj_get_parent(scroller);
		if (scroller) {
			lv_obj_scroll_by(scroller, 0, bottom - box.y2, LV_ANIM_OFF);
			lv_obj_update_layout(lv_screen_active());
			lv_obj_get_coords(target, &box);
		}
	}
	int x = (box.x1 + box.x2) / 2, y = (box.y1 + box.y2) / 2;
	printf("UI_UPSTREAM_CLICK %d,%d player-drag=%d back-drag=%d\n", x, y, player_sheet_drag_active(), switcher_back_drag_active());
	assert(x >= 0 && x < 480 && y >= 32 && y < lv_display_get_vertical_resolution(lv_display_get_default()));
	input(x, y, true);
	input(x, y, false);
}

static lv_obj_t *upstream_label(const char *text) {
	lv_obj_t *label = upstream_find(lv_screen_active(), text);
	assert(label && visible(label));
	return label;
}

static void upstream_capture(const char *name, bool dark) {
	char file[100];
	snprintf(file, sizeof(file), "upstream-%s-%s", name, dark ? "dark" : "light");
	capture(file);
}

static void upstream_run(void) {
	static int drag_x, drag_y;
	static unsigned drag_revision;
	static lv_obj_t *drag_row;
	int step = phase++;
	if (!step) {
		gui_init_rest();
		build_rows();
		sx_mini_set_track("Time", "/fixture/Time.wav", "Pink Floyd", false);
		sx_mini_set_playing(false);
		return;
	}
	int iteration = (step - 1) / 33;
	int state = (step - 1) % 33;
	printf("UI_UPSTREAM_STEP %d theme %d\n", state, iteration);
	if (iteration >= 2) {
		printf("UI_UPSTREAM_PASS PEQ numeric entry/cancel, history, lyrics, images, queue and recovery in both themes\n");
		fflush(stdout);
		exit(0);
	}
	bool dark = iteration == 0;
	switch (state) {
	case 0:
		theme_set_dark(dark);
		theme_set_accent(THEME_ACCENT_GRAY);
		peq_set_enabled(true);
		switch_screen(peqpage_screen());
		break;
	case 1: {
		upstream_capture("peq", dark);
		char title[80];
		snprintf(title, sizeof(title), tr("peq_band"), 1);
		upstream_click(upstream_label(title));
		break;
	}
	case 2: {
		upstream_capture("peq-band", dark);
		peq_band_t band;
		peq_get_band(0, &band);
		char gain[40];
		int value = band.gain_tenths;
		snprintf(gain, sizeof(gain), "%s%d,%d dB", value < 0 ? "-" : value > 0 ? "+" : "", abs(value) / 10, abs(value) % 10);
		upstream_click(upstream_label(gain));
		break;
	}
	case 3:
		assert(!sx_mini_visible());
		upstream_capture("peq-keypad", dark);
		upstream_click(upstream_label("5"));
		break;
	case 4:
		upstream_click(upstream_label(tr("ok")));
		break;
	case 5: {
		peq_band_t band;
		peq_get_band(0, &band);
		assert(band.gain_tenths == 50 && sx_mini_visible());
		upstream_click(upstream_label("+5,0 dB"));
		break;
	}
	case 6:
		assert(!sx_mini_visible());
		upstream_click(find_image(lv_screen_active(), &icon_close));
		break;
	case 7: {
		assert(sx_mini_visible());
		lrclib_record_t *records = calloc(2, sizeof(*records));
		assert(records);
		for (int i = 0; i < 2; i++) {
			snprintf(records[i].track, sizeof(records[i].track), "Time%s", i ? " - alternative" : "");
			snprintf(records[i].artist, sizeof(records[i].artist), "Pink Floyd");
			snprintf(records[i].album, sizeof(records[i].album), "The Dark Side of the Moon");
			records[i].duration = 412;
			records[i].synced = !i;
			records[i].text = strdup(i ? "Fixture lyrics\nSecond line" : "[00:00.00]Fixture lyrics\n[00:01.00]Second line");
		}
		lyricsearch_open(getenv("SONIX_UI_TEST_TRACK"), records, 2);
		break;
	}
	case 8:
		upstream_capture("lyrics-list", dark);
		upstream_click(upstream_label("Time"));
		break;
	case 9:
		upstream_capture("lyrics-preview", dark);
		assert(visible(upstream_find(lv_screen_active(), "Fixture lyrics")));
		imageview_open(getenv("SONIX_UI_TEST_IMAGE"));
		break;
	case 10:
		upstream_capture("image", dark);
		assert(!sx_mini_visible());
		imageview_open("/fixture/no-such-image.png");
		break;
	case 11:
		upstream_capture("image-error", dark);
		for (int i = 0; i < 20; i++)
			library_history_add(getenv("SONIX_UI_TEST_TRACK"), "Time", "Pink Floyd", 1800000000 + i);
		historypage_open(NULL);
		break;
	case 12:
		upstream_capture("history", dark);
		upstream_label("Time");
		switch_screen(metronomepage_screen);
		break;
	case 13: {
		upstream_capture("metronome", dark);
		assert(!sx_mini_visible());
		lv_obj_t *glyph = find_image(metronomepage_screen, &icon_play_large);
		assert(glyph && visible(glyph));
		assert(lv_color_eq(lv_obj_get_style_image_recolor(glyph, 0), sx_pal()->on_accent));
		recoverywizard_open(NULL);
		break;
	}
	case 14:
		upstream_capture("recovery", dark);
		assert(!recoverywizard_busy());
		upstream_label(tr("recovery_wizard_unsupported"));
		{
			const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT"), getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT")};
			playlist_load_paths_ordered(paths, 4, 0);
			assert(!playlist_move_to(0, 2));
			assert(playlist_move_to(3, 1));
			assert(playlist_current_index() == 0);
			assert(playlist_move(1, 1));
			trackmenu_open_queue();
		}
		break;
	case 15:
		upstream_capture("queue", dark);
		upstream_click(find_image(queue_screen, &icon_ellipsis_vertical));
		break;
	case 16:
		upstream_click(upstream_find(lv_layer_top(), tr("queue_select")));
		break;
	case 17:
		upstream_capture("queue-select", dark);
		upstream_click(upstream_label("Echoes"));
		break;
	case 18:
		upstream_click(find_image(queue_screen, &icon_ellipsis_vertical));
		break;
	case 19:
		upstream_click(upstream_find(lv_layer_top(), tr("queue_remove_selected")));
		break;
	case 20:
		assert(playlist_count() == 3 && playlist_current_index() == 0);
		upstream_capture("queue-removed", dark);
		upstream_click(upstream_find(lv_layer_top(), tr("sx_undo")));
		break;
	case 21:
		assert(playlist_count() == 4 && playlist_current_index() == 0);
		upstream_click(find_image(queue_screen, &icon_ellipsis_vertical));
		break;
	case 22:
		upstream_click(upstream_find(lv_layer_top(), tr("queue_reorder")));
		break;
	case 23: {
		upstream_capture("queue-reorder", dark);
		lv_obj_t *grip = find_image(queue_screen, &icon_grip);
		assert(grip && visible(grip));
		// The image lives in the trail, inside the content, inside its row.
		drag_row = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(grip)));
		assert(drag_row && lv_obj_get_style_bg_opa(drag_row, 0) == LV_OPA_TRANSP);
		lv_area_t box;
		lv_obj_get_coords(grip, &box);
		drag_x = (box.x1 + box.x2) / 2;
		drag_y = (box.y1 + box.y2) / 2;
		drag_revision = playlist_revision();
		input(drag_x, drag_y, true);
		input(drag_x, drag_y + 24, true);
		input(drag_x, drag_y + 190, true);
		assert(playlist_revision() == drag_revision);
		break;
	}
	case 24: {
		upstream_capture("queue-drag-held", dark);
		lv_area_t row_box, body_box;
		lv_obj_get_coords(drag_row, &row_box);
		lv_obj_get_coords(lv_obj_get_parent(drag_row), &body_box);
		assert(row_box.y1 >= body_box.y1 && row_box.y2 <= body_box.y2);
		assert(lv_obj_get_style_bg_opa(drag_row, 0) == LV_OPA_COVER);
		input(drag_x, drag_y + 190, false);
		assert(playlist_revision() != drag_revision && playlist_current_index() == 0);
		assert(lv_obj_get_style_bg_opa(drag_row, 0) == LV_OPA_TRANSP);
		break;
	}
	case 25:
		upstream_capture("queue-after-drop", dark);
		upstream_click(find_image(queue_screen, &icon_ellipsis_vertical));
		break;
	case 26:
		upstream_capture("queue-done-menu", dark);
		upstream_click(upstream_find(lv_layer_top(), tr("done")));
		break;
	case 27:
		// Finish the preceding reorder feedback before testing controls beneath it.
		sx_toast_dismiss();
		playlist_set_enqueue_ask();
		switch_screen(musicsettings_playback_screen());
		break;
	case 28:
		assert(playlist_enqueue_ask());
		upstream_click(upstream_label(tr("queue_add_end")));
		break;
	case 29:
		upstream_capture("queue-add-policy", dark);
		assert(!playlist_enqueue_ask() && playlist_enqueue_at_end());
		upstream_click(upstream_label(tr("queue_add_next")));
		break;
	case 30:
		assert(!playlist_enqueue_ask() && !playlist_enqueue_at_end());
		upstream_click(upstream_label(tr("queue_add_ask")));
		break;
	case 31:
		assert(playlist_enqueue_ask());
		switch_screen(main_menu_screen);
		break;
	case 32:
		upstream_capture("home", dark);
		break;
	}
}
