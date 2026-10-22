// Isolated regressions for seek misses, chapter navigation and large M3U.
#include "src/gui/nowplaying/chapters.h"
#include "src/gui/nowplaying/trackmenu.h"
#include "src/system/library/playlists.h"

static void feedback_large_m3u(void) {
	char source[1024], other[1024];
	snprintf(source, sizeof(source), "%s/Feedback large.m3u", getenv("SONIX_SD_ROOT"));
	snprintf(other, sizeof(other), "%s/Graphite/Echoes.flac", getenv("SONIX_SD_ROOT"));
	const int sizes[] = {5001, 20000, 20001};
	for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
		FILE *f = fopen(source, "w");
		assert(f);
		for (int i = 0; i < sizes[k]; i++)
			fprintf(f, "#EXTINF:60,Entry %d\n%s\n", i, i % 2 ? other : getenv("SONIX_UI_TEST_TRACK"));
		assert(fclose(f) == 0);
		playlists_import_result_t result;
		bool ok = playlists_import(source, true, &result);
		if (sizes[k] <= 20000) {
			assert(ok && result.total == sizes[k] && result.found == sizes[k]);
			assert(result.missing == 0 && result.by_name == 0);
			assert(library_playlist_count(result.name) == sizes[k]);
			library_playlist_row_t row;
			assert(library_playlist_page(result.name, sizes[k] - 1, 1, &row) == 1);
			assert(!strcmp(row.path, sizes[k] % 2 ? getenv("SONIX_UI_TEST_TRACK") : other));
		} else {
			assert(!ok && library_playlist_count("Feedback large") == 20000);
		}
	}
	char backup[1024];
	assert(playlists_backup("Feedback large", backup, sizeof(backup)));
	FILE *f = fopen(backup, "r");
	assert(f);
	char line[2048];
	int rows = 0;
	while (fgets(line, sizeof(line), f)) if (!strncmp(line, "#EXTINF:", 8)) rows++;
	fclose(f);
	assert(rows == 20000);
	library_index_t *index = library_index_open(LIBRARY_LIST_PLAYLIST, LIBRARY_FILTER_NONE,
		"Feedback large", LIBRARY_ORDER_DEFAULT, false);
	assert(index && library_index_count(index) == 20000);
	assert(playlist_load_index(index, 19999));
	assert(playlist_is_library_backed() && playlist_count() == 20000);
	char tail[512];
	assert(playlist_current_path(tail, sizeof(tail)) && !strcmp(tail, other));
	assert(playlist_prev(tail, sizeof(tail)) && !strcmp(tail, getenv("SONIX_UI_TEST_TRACK")));
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
	assert(playlist_count() == 20000 && playlist_path_at(19999, tail, sizeof(tail)));
	playlist_set_mode(PLAYBACK_MODE_NORMAL);
	printf("UI_TEST_PASS M3U 5001/20000 import/tail/backup and 20001 rollback\n");
	printf("UI_TEST_PASS imported 20000-row queue stays library-backed; tail/Previous/shuffle intact\n");
}

static bool feedback_history_count(const char *file, long long start, void *user) {
	(void)file; (void)start;
	(*(int *)user)++;
	return true;
}

static void feedback_history_roundtrip(const char *file, bool parts) {
	char book[512], database[1024], empty[1024];
	assert(audiobook_current_book(book, sizeof(book), NULL, 0));
	snprintf(database, sizeof(database), "%s/.local/audiobooks.db", getenv("SONIX_SD_ROOT"));
	snprintf(empty, sizeof(empty), "%s/.local/history-empty-test.db", getenv("SONIX_SD_ROOT"));
	audiobookdb_queue_chapter_done(book, file, parts ? -1 : 0);
	for (int i = 0; i < 32; i++) audiobookdb_queue_position(book, file, i * 0.1);
	int count = 0;
	audiobookdb_chapter_history(book, feedback_history_count, &count);
	assert(count == 1);
	audiobookdb_close();
	assert(audiobookdb_open(database));
	audiobook_track_changed(file); // Same path must reload after a DB epoch change.
	assert(parts ? audiobook_part_listened(0) : audiobook_chapter_listened(0));
	assert(parts ? !audiobook_part_listened(1) : !audiobook_chapter_listened(1));
	audiobookdb_close();
	assert(audiobookdb_open(empty));
	audiobook_track_changed(file);
	assert(!audiobook_is_playing());
	assert(!audiobook_part_listened(0) && !audiobook_chapter_listened(0));
	audiobookdb_close();
	assert(audiobookdb_open(database));
	audiobook_track_changed(file);
	assert(parts ? audiobook_part_listened(0) : audiobook_chapter_listened(0));
	printf("UI_TEST_PASS %s history survives coalesced checkpoints/reopen and does not leak to another card database\n", parts ? "part" : "chapter");
}

static void feedback_swipe(bool next) {
	int x = next ? 350 : 130;
	int end = next ? 130 : 350;
	input(x, 260, true);
	input((x + end) / 2, 260, true);
	input(end, 260, false);
}

static void feedback_states(void) {
	static int step;
	static char single[1024], part[1024];
	device_state_t state;
	switch (step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dark(true);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		{
			char books[1024];
			snprintf(books, sizeof(books), "%s/Audiobooks", getenv("SONIX_SD_ROOT"));
			assert(audiobookdb_scan_start(books));
		}
		break;
	case 1:
		if (library_scan_running() || audiobookdb_scan_running()) { step--; break; }
		feedback_large_m3u();
		player_layout_set(PLAYER_LAYOUT_STANDARD);
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		player_sheet_open(false);
		break;
	case 2: {
		// The large import may overrun the runner period. Flush pending sheet
		// geometry before the next timer callback injects pointer coordinates.
		lv_obj_update_layout(lv_layer_top());
		assert(player_sheet_is_open() && lv_obj_get_y(player_screen) == 0);
		device_state_get(&state);
		audio_status_t before = state.status;
		input(16, 260, true);
		input(16, 260, false);
		device_state_get(&state);
		assert(state.status == before);
		lv_obj_t *slider = find_slider(player_screen);
		assert(slider);
		lv_area_t bounds;
		lv_obj_get_coords(slider, &bounds);
		assert(bounds.y1 > 0 && bounds.y2 < lv_display_get_vertical_resolution(lv_display_get_default()));
		input(240, bounds.y1 - 12, true);
		input(240, bounds.y1 - 12, false);
		device_state_get(&state);
		assert(state.status == before);
		input(240, 260, true);
		input(240, 260, false);
		device_state_get(&state);
		assert(state.status != before);
		capture("cover-tap-only-dark");
		printf("UI_TEST_PASS artwork tap toggles; margin and seek miss preserve status\n");
		snprintf(single, sizeof(single), "%s/Audiobooks/Feedback single.flac", getenv("SONIX_SD_ROOT"));
		snprintf(part, sizeof(part), "%s/Audiobooks/Feedback parts/1.flac", getenv("SONIX_SD_ROOT"));
		char previous[1024];
		snprintf(previous, sizeof(previous), "%s/Graphite/Echoes.flac", getenv("SONIX_SD_ROOT"));
		const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), previous};
		device_state_play_list_ordered(paths, 2, 1);
		player_refresh_now_playing();
		break;
	}
	case 3:
		assert(playlist_current_index() == 1);
		device_state_seek(30);
		break;
	case 4:
		device_state_get(&state);
		assert(state.progress_current_secs > 3);
		player_key_prev();
		assert(playlist_current_index() == 1);
		break;
	case 5:
		device_state_get(&state);
		assert(state.progress_current_secs < 3);
		// A second headset hold arrives after playback passed three seconds.
		device_state_seek(4.2);
		lv_tick_inc(4000);
		break;
	case 6:
		device_state_get(&state);
		assert(state.progress_current_secs > 3);
		player_key_prev();
		break;
	case 7:
		assert(playlist_current_index() == 0);
		device_state_get(&state);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		printf("UI_TEST_PASS repeated headset Previous skips after delayed second hold\n");
		player_play_file(single);
		break;
	case 8:
		assert(audiobook_is_playing() && audiobook_chapter_count() == 3);
		device_state_seek(45);
		break;
	case 9:
		feedback_swipe(true);
		break;
	case 10:
		device_state_get(&state);
		assert(audiobook_chapter_at(state.progress_current_secs) == 2);
		feedback_swipe(false);
		break;
	case 11:
		device_state_get(&state);
		assert(audiobook_chapter_at(state.progress_current_secs) == 1);
		trackmenu_open_queue();
		assert(lv_screen_active() == chapters_screen);
		break;
	case 12:
		capture("chapters-dark");
		assert(!audiobook_chapter_listened(0) && !audiobook_chapter_listened(1));
		assert(chapters_step(-1));
		device_state_get(&state);
		assert(audiobook_chapter_at(state.progress_current_secs) == 0 && !chapters_step(-1));
		feedback_history_roundtrip(single, false);
		theme_set_dark(false);
		chapters_open();
		capture("chapters-light");
		player_play_file(part);
		player_sheet_open(false);
		break;
	case 13:
		assert(audiobook_part_count() == 2 && audiobook_part_current() == 0);
		feedback_swipe(true);
		break;
	case 14:
		assert(audiobook_part_current() == 1 && !chapters_step(1));
		feedback_swipe(false);
		break;
	case 15:
		assert(audiobook_part_current() == 0);
		trackmenu_open_queue();
		assert(lv_screen_active() == chapters_screen);
		break;
	case 16:
		assert(!audiobook_part_listened(0) && !audiobook_part_listened(1));
		feedback_history_roundtrip(part, true);
		chapters_refresh();
		capture("parts-light");
		printf("UI_TEST_PASS embedded/folder chapter swipes, boundaries and book queue route\n");
		fflush(stdout);
		exit(0);
	}
}
