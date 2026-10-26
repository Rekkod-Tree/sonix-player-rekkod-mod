// Full LVGL input dispatch, including the real pointer-level edge observer.
static unsigned gesture_holds, gesture_guards;
static uint32_t gesture_since;
static void gesture_hold(lv_obj_t *obj, void *user) {
	(void)obj;
	(void)user;
	gesture_holds++;
}
static bool gesture_guard(void) {
	gesture_guards++;
	return true;
}
static void gesture_nested_page(void) {
	switcher_set_back_guard(rows_screen, NULL);
	screen_history_reset();
	switch_screen_no_history(main_menu_screen);
	switch_screen(music_screen);
	switch_screen(rows_screen);
}
static void gesture_navigation_states(lv_timer_t *timer) {
	static int step;
	static unsigned revision;
	static int dwell_moves;
	switch (step++) {
	case 0:
		gui_init_rest();
		// Reuse the real emulator pointer: switcher watches this device, not
		// a second test-only pointer which would miss edge arbitration.
		for (pointer = lv_indev_get_next(NULL); pointer; pointer = lv_indev_get_next(pointer))
			if (lv_indev_get_type(pointer) == LV_INDEV_TYPE_POINTER)
				break;
		assert(pointer);
		lv_indev_set_read_cb(pointer, read_pointer);
		lv_timer_set_period(timer, 250);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1: {
		if (library_scan_running()) {
			step--;
			break;
		}
		medialist_set_album_head(true);
		rows_screen = lv_obj_create(NULL);
		lv_obj_add_style(rows_screen, &sx_style_page, 0);
		sx_row_handlers_t handlers = {.on_tap = tap, .on_star = like, .on_swipe_next = next, .on_long_press = gesture_hold};
		track_row = row(140, "Time", &handlers);
		sx_row_set_trail(track_row, SX_ROW_TRAIL_STAR);
		switch_screen(main_menu_screen);
		switch_screen(rows_screen);
		break;
	}
	case 2:
		input(34, 180, true);
		input(80, 195, true);
		break;
	case 3:
		assert(switcher_back_drag_active() && !taps && !likes && !nexts && !gesture_holds);
		input(210, 210, true);
		capture("gesture-edge-back-held");
		break;
	case 4:
		input(210, 210, false);
		break;
	case 5:
		assert(lv_screen_active() == main_menu_screen && !taps && !nexts && !likes && !gesture_holds);
		switch_screen(rows_screen);
		break;
	case 6:
		input(54, 180, true);
		input(220, 180, true);
		input(220, 180, false);
		break;
	case 7:
		assert(lv_screen_active() == rows_screen && !taps && !nexts && !likes);
		input(150, 180, true);
		input(290, 180, true);
		input(290, 180, false);
		break;
	case 8:
		assert(nexts == 1 && !likes && !taps);
		input(430, 180, true);
		input(230, 180, true);
		input(230, 180, false);
		break;
	case 9:
		assert(nexts == 1 && !likes && !taps);
		input(340, 180, true);
		input(200, 180, true);
		input(200, 180, false);
		break;
	case 10:
		assert(likes == 1 && nexts == 1 && !taps);
		input(200, 180, true);
		input(218, 180, true);
		input(200, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 11:
		if (lv_tick_elaps(gesture_since) < 700) {
			step--;
			break;
		}
		assert(!gesture_holds);
		input(200, 180, false);
		assert(!taps);
		input(200, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 12:
		if (lv_tick_elaps(gesture_since) < 700) {
			step--;
			break;
		}
		assert(gesture_holds == 1);
		input(200, 180, false);
		assert(!taps);
		input(52, 88, true);
		gesture_since = lv_tick_get();
		break;
	case 13:
		if (lv_tick_elaps(gesture_since) < 700) {
			step--;
			break;
		}
		input(52, 88, false);
		assert(lv_screen_active() == main_menu_screen);
		capture("gesture-back-hold-home");
		switcher_browse_begin("music");
		medialist_open(tr("albums"), LIBRARY_LIST_ALBUMS, LIBRARY_FILTER_NONE, NULL);
		medialist_open("The Dark Side of the Moon", LIBRARY_LIST_TRACKS, LIBRARY_FILTER_ALBUM, "The Dark Side of the Moon");
		break;
	case 14:
		qe_click(find_label(medialist_tracks_screen, "Time"));
		break;
	case 15: {
		device_state_t state;
		device_state_get(&state);
		playbackplace_t place;
		if (!strstr(state.current_file, "Time.flac") || !playbackplace_get("music", &place) || strcmp(place.path, state.current_file)) {
			step--;
			break;
		}
		if (state.status == AUDIO_STATUS_PLAYING)
			device_state_toggle_play_pause();
		revision = playlist_revision();
		switch_screen(settings_screen);
		player_sheet_open(false);
		trackmenu_open(NULL);
		break;
	}
	case 16:
		assert(sx_sheet_is_open());
		capture("gesture-source-menu");
		order_click(find_label(lv_layer_top(), tr("playback_source_open")));
		break;
	case 17:
		assert(lv_screen_active() == medialist_tracks_screen && !player_sheet_is_open());
		assert(visible(find_label(medialist_tracks_screen, "Time")));
		capture("gesture-source-album");
		back_btn_cb(NULL);
		break;
	case 18:
		assert(lv_screen_active() == medialist_screen && !player_sheet_is_open());
		assert(find_label(lv_screen_active(), "Meddle"));
		assert(playlist_revision() == revision);
		capture("gesture-source-parent-albums");
		back_btn_cb(NULL);
		break;
	case 19:
		assert(lv_screen_active() == music_screen && !player_sheet_is_open());
		medialist_open("The Dark Side of the Moon", LIBRARY_LIST_TRACKS, LIBRARY_FILTER_ALBUM, "The Dark Side of the Moon");
		break;
	case 20: {
		lv_obj_t *icon = find_visible_image(medialist_tracks_screen, &icon_chevron_left);
		assert(icon);
		lv_area_t box;
		lv_obj_get_coords(icon, &box);
		input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
		gesture_since = lv_tick_get();
		break;
	}
	case 21:
		if (lv_tick_elaps(gesture_since) < 700) {
			step--;
			break;
		}
		input(point.x, point.y, false);
		assert(lv_screen_active() == main_menu_screen && playlist_revision() == revision);
		player_home_gestures();
		break;
	case 22:
		player_sheet_close(false);
		switch_screen(rows_screen);
		switcher_set_back_guard(rows_screen, gesture_guard);
		break;
	case 23:
		input(52, 88, true);
		gesture_since = lv_tick_get();
		break;
	case 24:
		if (lv_tick_elaps(gesture_since) < 700) {
			step--;
			break;
		}
		input(52, 88, false);
		assert(lv_screen_active() == rows_screen && gesture_guards == 1);
		assert(!sx_sheet_is_open() && !quickpanel_is_open());
		gesture_nested_page();
		break;
	case 25:
		input(30, 180, true);
		input(230, 180, true);
		break;
	case 26:
		input(230, 180, false); // A short dwell is ordinary Back.
		break;
	case 27:
		assert(lv_screen_active() == music_screen);
		gesture_nested_page();
		break;
	case 28:
		input(30, 180, true);
		input(230, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 29:
		if (lv_tick_elaps(gesture_since) < 1000) {
			step--;
			break;
		}
		assert(lv_screen_active() == rows_screen && switcher_back_drag_active());
		capture("gesture-edge-home-preview-held");
		input(230, 180, false);
		break;
	case 30:
		assert(lv_screen_active() == main_menu_screen);
		gesture_nested_page();
		break;
	case 31:
		input(30, 180, true);
		input(230, 180, true);
		break;
	case 32:
		if (dwell_moves++ < 4) {
			input(230 + dwell_moves * 20, 180, true);
			step--;
			break;
		}
		input(point.x, point.y, false);
		break;
	case 33:
		assert(lv_screen_active() == music_screen); // Movement restarts the dwell.
		gesture_nested_page();
		break;
	case 34:
		input(30, 180, true);
		input(230, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 35:
		if (lv_tick_elaps(gesture_since) < 1000) {
			step--;
			break;
		}
		input(130, 180, true); // Disarm Home, but keep an ordinary Back pull.
		break;
	case 36:
		input(130, 180, false);
		break;
	case 37:
		assert(lv_screen_active() == music_screen);
		gesture_nested_page();
		break;
	case 38:
		input(30, 180, true);
		input(230, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 39:
		if (lv_tick_elaps(gesture_since) < 1000) {
			step--;
			break;
		}
		input(45, 180, true); // Cancel the entire drag after arming Home.
		break;
	case 40:
		input(45, 180, false);
		break;
	case 41:
		assert(lv_screen_active() == rows_screen);
		gesture_nested_page();
		switcher_set_back_guard(rows_screen, gesture_guard);
		break;
	case 42:
		input(30, 180, true);
		input(230, 180, true);
		gesture_since = lv_tick_get();
		break;
	case 43:
		if (lv_tick_elaps(gesture_since) < 1000) {
			step--;
			break;
		}
		input(230, 180, false);
		break;
	case 44:
		assert(lv_screen_active() == rows_screen && gesture_guards == 2);
		assert(!sx_sheet_is_open() && !quickpanel_is_open());
		puts("UI_TEST_PASS gesture arbitration: expanded edge, neutral margins, central actions, moved hold cancellation, Home holds, album parent, player gestures, edge dwell Home/short/movement/cancel and service guard");
		fflush(stdout);
		exit(0);
	}
}
