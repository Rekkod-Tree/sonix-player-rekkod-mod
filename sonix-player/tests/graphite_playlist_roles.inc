// Shared playlist page: picker visits must not overwrite older browse visits.
#include "src/gui/nowplaying/trackmenu.h"
#include "src/system/library/playlists.h"
static lv_obj_t *role_detail;
static int role_step, role_wait;
static const char *role_source = "Role source";
static bool role_old;
static void role_title(bool picker) {
	assert(lv_screen_active() == playlistpage_screen && !player_sheet_is_open());
	assert(visible(find_label(playlistpage_screen, tr(picker ? "playlist_add_to_playlist_2" : "playlists"))));
}
static void role_queue_menu(void) {
	assert(lv_screen_active() == queue_screen && !player_sheet_is_open());
	lv_obj_t *glyph = find_visible_image(queue_screen, &icon_ellipsis_vertical);
	assert(glyph);
	qe_click(lv_obj_get_parent(glyph));
}
static void role_save_action(void) {
	lv_obj_t *label = find_label(lv_layer_top(), tr("queue_save_as_playlist"));
	assert(visible(label));
	qe_click(label);
}
static void role_import(void) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/Role source.m3u", getenv("SONIX_SD_ROOT"));
	FILE *f = fopen(path, "w");
	assert(f);
	fprintf(f, "#EXTM3U\n%s\n%s\n", getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT"));
	assert(!fclose(f));
	playlists_import_result_t result;
	assert(playlists_import(path, false, &result));
	assert(!strcmp(result.name, role_source));
	assert(library_playlist_count(role_source) == 2);
	library_index_t *ix = library_index_open(LIBRARY_LIST_PLAYLIST, LIBRARY_FILTER_NONE, role_source, LIBRARY_ORDER_DEFAULT, false);
	assert(ix && playlist_load_index(ix, 0));
	player_sheet_close(false);
	screen_history_reset();
	switch_screen_no_history(music_screen);
}
static void playlist_roles_states(void) {
	switch (role_step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		role_old = getenv("SONIX_UI_TEST_ROLES_EXPECT_OLD") != NULL;
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1:
		if (library_scan_running()) {
			assert(++role_wait < 30);
			role_step--;
			break;
		}
		role_import();
		break;
	case 2:
		playlistpage_open();
		break;
	case 3:
		role_title(false);
		qe_click(find_label(playlistpage_screen, role_source));
		break;
	case 4:
		role_detail = lv_screen_active();
		assert(role_detail == medialist_tracks_screen);
		switch_screen(player_screen);
		break;
	case 5:
		assert(player_sheet_is_open());
		trackmenu_open_queue();
		break;
	case 6:
		role_queue_menu();
		break;
	case 7:
		role_save_action();
		break;
	case 8:
		role_title(true);
		capture("roles-initial-picker");
		back_btn_cb(NULL);
		break;
	case 9:
		assert(lv_screen_active() == queue_screen);
		back_btn_cb(NULL);
		break;
	case 10:
		assert(player_sheet_is_open() && lv_screen_active() == role_detail);
		player_sheet_close(false);
		break;
	case 11:
		assert(lv_screen_active() == role_detail && !player_sheet_is_open());
		back_btn_cb(NULL);
		break;
	case 12:
		role_title(role_old);
		capture(role_old ? "roles-baseline-stale-picker" : "roles-history-browse-restored");
		assert(library_playlist_count(role_source) == 2);
		qe_click(find_label(playlistpage_screen, role_source));
		role_wait = 0;
		break;
	case 13:
		if (role_old) {
			if (library_playlist_count(role_source) != 4) {
				assert(++role_wait < 30);
				role_step--;
				break;
			}
			assert(lv_screen_active() != role_detail);
			capture("roles-baseline-unintended-append");
			puts("UI_TEST_PASS BASELINE role bug: cancelled picker later appended instead of opening historical playlist");
			fflush(stdout);
			exit(0);
		}
		assert(lv_screen_active() == role_detail && library_playlist_count(role_source) == 2);
		switch_screen(player_screen);
		break;
	case 14:
		assert(player_sheet_is_open());
		trackmenu_open_queue();
		break;
	case 15:
		role_queue_menu();
		break;
	case 16:
		role_save_action();
		break;
	case 17:
		role_title(true);
		qe_click(find_label(playlistpage_screen, tr("playlist_new_playlist")));
		break;
	case 18:
		assert(keyboard_shown_on(playlistpage_screen));
		capture("roles-name-cancel-before");
		qe_click(lv_obj_get_parent(find_visible_image(playlistpage_screen, &icon_close)));
		break;
	case 19:
		role_title(true);
		assert(!keyboard_shown_on(playlistpage_screen));
		qe_click(find_label(playlistpage_screen, tr("playlist_new_playlist")));
		break;
	case 20:
		assert(keyboard_shown_on(playlistpage_screen));
		lv_textarea_set_text(find_field(playlistpage_screen), "777");
		qe_click(qe_key(playlistpage_screen, tr("ok")));
		role_wait = 0;
		break;
	case 21:
		if (library_playlist_count("777") != 2) {
			assert(++role_wait < 30);
			role_step--;
			break;
		}
		assert(lv_screen_active() == queue_screen);
		puts("UI_TEST_PASS fresh queue picker name cancel/reentry and CREATE preserve owned worker");
		back_btn_cb(NULL);
		break;
	case 22:
		assert(player_sheet_is_open() && lv_screen_active() == role_detail);
		player_sheet_close(false);
		break;
	case 23:
		assert(lv_screen_active() == role_detail);
		back_btn_cb(NULL);
		break;
	case 24:
		role_title(false);
		capture("roles-after-create-browse");
		assert(library_playlist_count(role_source) == 2);
		screen_history_reset();
		switch_screen_no_history(music_screen);
		break;
	case 25:
		playlistpage_add_track(getenv("SONIX_UI_TEST_TRACK"));
		break;
	case 26:
		role_title(true);
		back_btn_cb(NULL);
		break;
	case 27:
		assert(lv_screen_active() == music_screen);
		playlistpage_open();
		break;
	case 28:
		role_title(false);
		screen_history_reset();
		switch_screen_no_history(music_screen);
		break;
	case 29: {
		const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT")};
		playlistpage_add_tracks(paths, 2);
		break;
	}
	case 30:
		role_title(true);
		qe_click(find_label(playlistpage_screen, "777"));
		role_wait = 0;
		break;
	case 31:
		if (library_playlist_count("777") != 4) {
			assert(++role_wait < 30);
			role_step--;
			break;
		}
		assert(lv_screen_active() == music_screen);
		playlistpage_open();
		break;
	case 32:
		role_title(false);
		assert(library_playlist_count("777") == 4 && library_playlist_count(role_source) == 2);
		capture("roles-final-browse");
		qe_click(find_label(playlistpage_screen, role_source));
		break;
	case 33: {
		assert(lv_screen_active() == role_detail);
		feedback_large_m3u();
		library_index_t *ix = library_index_open(LIBRARY_LIST_PLAYLIST, LIBRARY_FILTER_NONE, "Feedback large", LIBRARY_ORDER_DEFAULT, false);
		assert(ix && playlist_load_index(ix, 0));
		assert(playlist_count() == 20000);
		switch_screen(player_screen);
		break;
	}
	case 34:
		assert(player_sheet_is_open());
		trackmenu_open_queue();
		break;
	case 35:
		assert(lv_screen_active() == queue_screen);
		playlistpage_add_queue();
		break;
	case 36:
		role_title(true);
		qe_click(find_label(playlistpage_screen, "777"));
		break;
	case 37:
		assert(lv_screen_active() == queue_screen);
		assert(sx_toast_busy_showing());
		assert(library_playlist_count("777") == 4);
		// Each navigation result is checked; restore the older browse visit while the owned export is still active.
		back_btn_cb(NULL);
		assert(player_sheet_is_open() && lv_screen_active() == role_detail);
		player_sheet_close(false);
		assert(lv_screen_active() == role_detail && !player_sheet_is_open());
		back_btn_cb(NULL);
		role_title(false);
		assert(library_playlist_count("777") == 4);
		role_step = 40;
		break;
	case 40:
		role_title(false);
		capture("roles-browse-during-20000-export");
		qe_click(find_label(playlistpage_screen, role_source));
		role_wait = 0;
		break;
	case 41: {
		assert(lv_screen_active() == role_detail && !player_sheet_is_open());
		if (!role_wait && sx_toast_busy_showing()) {
			qe_click(find_label(lv_layer_top(), tr("playlist_adding_tracks")));
		}
		if (library_playlist_count("777") != 20004) {
			assert(++role_wait < 80);
			role_step--;
			break;
		}
		library_playlist_row_t row;
		assert(library_playlist_page("777", 4, 1, &row) == 1 && !strcmp(row.path, getenv("SONIX_UI_TEST_TRACK")));
		assert(library_playlist_page("777", 20003, 1, &row) == 1 && !strcmp(row.path, getenv("SONIX_UI_TEST_NEXT")));
		assert(library_playlist_count(role_source) == 2);
		capture("roles-owned-export-complete");
		puts("UI_TEST_PASS PLAYLIST_ROLES historical role restored during live 20000 export; explicit queue/single/multi picker, cancel/name/CREATE/APPEND retained");
		fflush(stdout);
		exit(0);
	}
	}
}
