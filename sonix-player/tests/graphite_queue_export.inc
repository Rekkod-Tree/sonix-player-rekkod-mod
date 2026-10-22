// Full application queue export through the real picker and its worker.
static void qe_click(lv_obj_t *obj) {
	assert(obj && visible(obj) && !player_sheet_is_open());
	lv_obj_update_layout(lv_screen_active());
	lv_obj_update_layout(lv_layer_top());
	lv_area_t a;
	lv_obj_get_coords(obj, &a);
	assert(a.x1 >= 0 && a.x2 < 480 && a.y1 >= 0);
	assert(a.y2 < lv_display_get_vertical_resolution(lv_display_get_default()));
	input((a.x1+a.x2)/2, (a.y1+a.y2)/2, true);
	input((a.x1+a.x2)/2, (a.y1+a.y2)/2, false);
}
static lv_obj_t *qe_key(lv_obj_t *root, const char *text) {
	if (!visible(root)) return NULL;
	if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) {
		lv_area_t a; lv_obj_get_coords(root, &a);
		if (a.y1 >= lv_display_get_vertical_resolution(lv_display_get_default()) - 316) return root;
	}
	for (uint32_t i=0; i<lv_obj_get_child_count(root); i++) {
		lv_obj_t *key = qe_key(lv_obj_get_child(root, (int)i), text);
		if (key) return key;
	}
	return NULL;
}
static void qe_capture(const char *state, bool dark) {
	char name[100];
	snprintf(name, sizeof(name), "queue-export-%s-%s", dark ? "dark" : "light", state);
	capture(name);
}
static void qe_verify(const char *name, bool append) {
	library_playlist_row_t row;
	assert(library_playlist_count(name) == 20000 + (append ? 1 : 0));
	assert(library_playlist_page(name, 0, 1, &row) == 1);
	assert(!strcmp(row.path, append ? getenv("SONIX_UI_TEST_NEXT") : getenv("SONIX_UI_TEST_TRACK")));
	assert(library_playlist_page(name, append ? 1 : 0, 1, &row) == 1);
	assert(!strcmp(row.path, getenv("SONIX_UI_TEST_TRACK")));
	assert(library_playlist_page(name, append ? 20000 : 19999, 1, &row) == 1);
	assert(!strcmp(row.path, getenv("SONIX_UI_TEST_NEXT")));
}
static void queue_export_states(void) {
	static int step, waits;
	static bool dark;
	static char target[64], created[64];
	switch (step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dark(false);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1:
		if (library_scan_running()) { assert(++waits < 30); step--; break; }
		feedback_large_m3u();
		waits = 0;
		break;
	case 2: {
		theme_set_dark(dark);
		snprintf(target, sizeof(target), "A append %s", dark ? "dark" : "light");
		snprintf(created, sizeof(created), "%s", dark ? "222" : "111");
		assert(playlists_create(target));
		assert(playlists_add_track(target, getenv("SONIX_UI_TEST_NEXT")));
		library_index_t *ix = library_index_open(LIBRARY_LIST_PLAYLIST, LIBRARY_FILTER_NONE,
			"Feedback large", LIBRARY_ORDER_DEFAULT, false);
		assert(ix && playlist_load_index(ix, 0));
		player_sheet_close(false);
		switch_screen_no_history(music_screen);
		break;
	}
	case 3:
		lv_obj_update_layout(lv_layer_top());
		assert(!player_sheet_is_open());
		playlistpage_add_queue();
		break;
	case 4:
		assert(lv_screen_active() == playlistpage_screen && !player_sheet_is_open());
		qe_capture("append-picker", dark);
		qe_click(find_label(playlistpage_screen, target));
		qe_capture("append-busy", dark);
		waits = 0;
		break;
	case 5:
		assert(!player_sheet_is_open());
		if (!waits && sx_toast_busy_showing()) {
			lv_obj_t *notice = find_label(lv_layer_top(), tr("playlist_adding_tracks"));
			assert(notice && visible(notice));
			qe_click(notice);
			assert(!sx_toast_showing());
			printf("UI_TEST_PASS dismiss busy toast before export finishes\n");
		}
		if (library_playlist_count(target) != 20001) { assert(++waits < 80); step--; break; }
		qe_verify(target, true);
		qe_capture("append-complete", dark);
		printf("UI_TEST_PASS queue APPEND 20000 retains original row and exact endpoints %s\n", target);
		break;
	case 6:

		playlistpage_add_queue();
		break;
	case 7:
		assert(lv_screen_active() == playlistpage_screen && !player_sheet_is_open());
		qe_click(find_label(playlistpage_screen, tr("playlist_new_playlist")));
		break;
	case 8: {
		lv_obj_t *field = find_field(playlistpage_screen);
		assert(field && visible(field));
		qe_capture("create-form", dark);
		// Switch the actual keyboard to its numeric layout, then type the name.
		qe_click(qe_key(playlistpage_screen, "123"));
		break;
	}
	case 9:
		qe_click(qe_key(playlistpage_screen, dark ? "2" : "1"));
		break;
	case 10:
		qe_click(qe_key(playlistpage_screen, dark ? "2" : "1"));
		break;
	case 11:
		qe_click(qe_key(playlistpage_screen, dark ? "2" : "1"));
		break;
	case 12:
		printf("QUEUE_KEYBOARD actual=[%s] expected=[%s]\n", lv_textarea_get_text(find_field(playlistpage_screen)), created);
		assert(!strcmp(lv_textarea_get_text(find_field(playlistpage_screen)), created));
		qe_capture("create-named", dark);
		qe_click(qe_key(playlistpage_screen, tr("ok")));
		waits = 0;
		break;
	case 13:
		if (library_playlist_count(created) != 20000) {
			if (++waits == 1) qe_capture("create-wait", dark);
			if (waits >= 80) qe_capture("create-timeout", dark);
			assert(waits < 80); step--; break;
		}
		qe_verify(created, false);
		qe_capture("create-complete", dark);
		printf("UI_TEST_PASS queue CREATE 20000 via numeric keyboard preserves order %s\n", created);
		break;
	case 14: {
		const char **paths = calloc(20001, sizeof(*paths));
		assert(paths);
		for (int i=0; i<20001; i++) paths[i] = getenv("SONIX_UI_TEST_TRACK");
		playlist_load_paths_ordered(paths, 20001, 0);
		free(paths);
		assert(playlist_count() == 20001);
		switch_screen_no_history(music_screen);
		break;
	}
	case 15:
		assert(!player_sheet_is_open());
		playlistpage_add_queue();
		assert(lv_screen_active() == music_screen);
		break;
	case 16:
		assert(lv_screen_active() == music_screen);
		assert(visible(find_label(lv_layer_top(), tr("playlist_queue_too_large"))));
		qe_verify(target, true);
		qe_verify(created, false);
		qe_capture("20001-refused", dark);
		printf("UI_TEST_PASS 20001 refusal keeps original screen and both targets unchanged\n");
		if (!dark) { dark = true; step = 2; } else { printf("UI_TEST_PASS QUEUE_EXPORT all GUI scenarios\n"); fflush(stdout); exit(0); }
		break;
	}
}
