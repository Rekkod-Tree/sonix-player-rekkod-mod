// Queue modes use the real queue, not a second preview order.
static void order_click(lv_obj_t *obj) {
	assert(obj && visible(obj));
	lv_obj_update_layout(lv_screen_active());
	lv_obj_update_layout(lv_layer_top());
	lv_area_t box;
	lv_obj_get_coords(obj, &box);
	input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
	input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, false);
}

static void queue_mode_states(lv_timer_t *timer) {
	static int step, saved_index;
	static uint32_t since;
	static char current[512], first[512], second[512];
	static double paused_at;
	switch (step++) {
	case 0:
		gui_init_rest();
		lv_timer_set_period(timer, 250);
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1:
		if (library_scan_running()) {
			step--;
			break;
		}
		assert(library_fav_toggle(getenv("SONIX_UI_TEST_TRACK"), "Time", "Pink Floyd"));
		assert(library_fav_toggle(getenv("SONIX_UI_TEST_NEXT"), "Echoes", "Pink Floyd"));
		medialist_open("Favourites", LIBRARY_LIST_FAVOURITES, LIBRARY_FILTER_NONE, NULL);
		break;
	case 2:
		order_click(find_label(medialist_tracks_screen, "Time"));
		break;
	case 3: {
		device_state_t state;
		device_state_get(&state);
		if (!strstr(state.current_file, "Time.flac")) {
			step--;
			break;
		}
		if (state.status == AUDIO_STATUS_PLAYING)
			device_state_toggle_play_pause();
		assert(playlist_count() == 2);
		snprintf(current, sizeof(current), "%s", state.current_file);
		playlist_set_mode(PLAYBACK_MODE_SHUFFLE_REPEAT);
		player_refresh_now_playing();
		player_sheet_open(false);
		player_playback_mode_menu();
		break;
	}
	case 4:
		assert(sx_sheet_is_open());
		assert(find_label(lv_layer_top(), tr("player_mode_normal")));
		assert(find_label(lv_layer_top(), tr("player_mode_repeat_one")));
		assert(find_label(lv_layer_top(), tr("player_mode_shuffle")));
		capture("queue-five-explicit-modes");
		order_click(find_label(lv_layer_top(), tr("player_mode_repeat_all")));
		break;
	case 5: {
		assert(playlist_get_mode() == PLAYBACK_MODE_REPEAT_ALL && playlist_count() == 2);
		char path[512];
		assert(playlist_current_path(path, sizeof(path)) && !strcmp(path, current));
		saved_index = playlist_current_index();
		assert(playlist_path_at(0, first, sizeof(first)) && playlist_path_at(1, second, sizeof(second)));
		assert(strcmp(first, second));
		assert(playlist_set_current(1));
		assert(playlist_peek_auto(path, sizeof(path)) && !strcmp(path, first));
		assert(playlist_advance_auto(path, sizeof(path)) && !strcmp(path, first));
		assert(playlist_advance_auto(path, sizeof(path)) && !strcmp(path, second));
		assert(playlist_set_current(saved_index));
		player_refresh_now_playing();
		trackmenu_open_queue();
		break;
	}
	case 6: {
		assert(lv_screen_active() == queue_screen && !player_sheet_is_open());
		assert(find_label(queue_screen, "Time") && find_label(queue_screen, "Echoes"));
		int position, total;
		assert(player_queue_position(&position, &total));
		assert(position == playlist_current_index() + 1 && total == 2);
		capture("queue-repeat-favourites-order");
		lv_obj_t *title = find_label(queue_screen, "Echoes");
		lv_area_t box;
		lv_obj_get_coords(title, &box);
		input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
		since = lv_tick_get();
		break;
	}
	case 7:
		if (lv_tick_elaps(since) < 700) {
			step--;
			break;
		}
		input(point.x, point.y, false);
		assert(sx_sheet_is_open());
		{
			const char *paths[] = {getenv("SONIX_UI_TEST_NEXT"), getenv("SONIX_UI_TEST_TRACK")};
			playlist_load_paths(paths, 2, 1);
		}
		order_click(find_label(lv_layer_top(), tr("queue_play_now")));
		break;
	case 8: {
		device_state_t state;
		device_state_get(&state);
		assert(!strcmp(state.current_file, current) && state.status == AUDIO_STATUS_PAUSED);
		assert(playlist_current_index() == 1 && playlist_count() == 2);
		paused_at = state.progress_current_secs;
		lv_obj_t *title = find_label(queue_screen, "Echoes");
		assert(title);
		lv_area_t box;
		lv_obj_get_coords(title, &box);
		input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
		const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT")};
		playlist_load_paths(paths, 2, 0);
		trackmenu_queue_poll(); // Must not replace a row under this press.
		input(point.x, point.y, false);
		break;
	}
	case 9: {
		device_state_t state;
		device_state_get(&state);
		assert(!strcmp(state.current_file, current) && state.status == AUDIO_STATUS_PAUSED && state.progress_current_secs == paused_at);
		assert(playlist_current_index() == 0 && playlist_count() == 2);
		playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
		player_refresh_now_playing();
		int position, count;
		assert(player_queue_position(&position, &count));
		assert(position == playlist_current_index() + 1 && count == 2);
		char path[512];
		assert(playlist_current_path(path, sizeof(path)) && !strcmp(path, current));
		// The actual next API walks exactly the order exposed to the queue.
		assert(playlist_set_current(0));
		assert(playlist_path_at(1, second, sizeof(second)));
		assert(playlist_next(path, sizeof(path)) && !strcmp(path, second));
		assert(playlist_set_current(1));
		capture("queue-after-stale-press-rejected");
		puts("UI_TEST_PASS favourites repeat-all, explicit five modes, current-track preservation, repeat wrap, actual next order, shared counter and stale row/menu rejection");
		fflush(stdout);
		exit(0);
	}
	}
}
