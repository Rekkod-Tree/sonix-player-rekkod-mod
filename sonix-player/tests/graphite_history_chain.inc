// Real history row input and player layout while source details settle.
static lv_obj_t *hc_slider(lv_obj_t *root) {
	if (lv_obj_check_type(root, &lv_slider_class))
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = hc_slider(lv_obj_get_child(root, (int)i));
		if (found)
			return found;
	}
	return NULL;
}

static void hc_setup(lv_timer_t *timer) {
	gui_init_rest();
	for (pointer = lv_indev_get_next(NULL); pointer; pointer = lv_indev_get_next(pointer))
		if (lv_indev_get_type(pointer) == LV_INDEV_TYPE_POINTER)
			break;
	assert(pointer);
	lv_indev_set_read_cb(pointer, read_pointer);
	lv_timer_set_period(timer, 250);
	assert(library_scan_start(getenv("SONIX_SD_ROOT")));
}

static void history_handoff_states(lv_timer_t *timer) {
	static int step, waiting;
	static double checkpoint;
	device_state_t state;
	device_state_get(&state);
	switch (step++) {
	case 0:
		hc_setup(timer);
		break;
	case 1: {
		if (library_scan_running()) {
			step--;
			break;
		}
		assert(library_history_record_at(library_database_epoch(), getenv("SONIX_UI_TEST_TRACK"), "Time", "Pink Floyd", 1) > 0);
		char root[512];
		snprintf(root, sizeof(root), "%s/Audiobooks", getenv("SONIX_SD_ROOT"));
		assert(audiobookdb_scan_start(root));
		break;
	}
	case 2:
		if (audiobookdb_scan_running()) {
			step--;
			break;
		}
		audiobooks_play_at(getenv("SONIX_UI_TEST_BOOK"), getenv("SONIX_UI_TEST_BOOK"), 17);
		break;
	case 3:
		if (state.status != AUDIO_STATUS_PLAYING || !playhistory_current_id()) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		assert(audiobook_is_playing());
		historypage_open(NULL);
		break;
	case 4:
		assert(find_label(lv_screen_active(), "Home resume"));
		capture("history-book-playing-mixed-rows");
		qe_click(find_label(lv_screen_active(), "Time"));
		waiting = 0;
		break;
	case 5:
		if (strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")) || state.status != AUDIO_STATUS_PLAYING) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		assert(!audiobook_is_playing());
		printf("HISTORY_HANDOFF_AUDIO_IS_MUSIC\n");
		assert(visible(find_label(player_screen, "Time")));
		assert(player_lyrics_available());
		capture("history-book-to-music-player");
		char file[512];
		assert(audiobook_resume_point(getenv("SONIX_UI_TEST_BOOK"), file, sizeof(file), &checkpoint));
		assert(checkpoint >= 17 && checkpoint < 30);
		player_sheet_close(false);
		historypage_open(NULL);
		break;
	case 6:
		qe_click(find_label(lv_screen_active(), "Home resume"));
		waiting = 0;
		break;
	case 7:
		if (strcmp(state.current_file, getenv("SONIX_UI_TEST_BOOK")) || state.status != AUDIO_STATUS_PLAYING) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		assert(audiobook_is_playing() && visible(find_label(player_screen, "Home resume")));
		assert(state.progress_current_secs >= checkpoint - .1 && state.progress_current_secs < checkpoint + 4);
		capture("history-music-to-book-resumed");
		puts("UI_TEST_PASS history: book to music updates audio/title/transport; music to book resumes checkpoint");
		fflush(stdout);
		exit(0);
	}
}

static void chain_layout_states(lv_timer_t *timer) {
	static int step, waiting;
	static lv_area_t before;
	device_state_t state;
	device_state_get(&state);
	switch (step++) {
	case 0:
		hc_setup(timer);
		btline_set_enabled(true);
		break;
	case 1:
		if (library_scan_running()) {
			step--;
			break;
		}
		player_sheet_open(false);
		lv_obj_update_layout(player_screen);
		lv_obj_get_coords(hc_slider(player_screen), &before);
		capture("chain-before-source-details");
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		break;
	case 2: {
		if (state.status != AUDIO_STATUS_PLAYING || state.stream_sample_rate <= 0) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		player_refresh_now_playing();
		lv_obj_update_layout(player_screen);
		lv_area_t after;
		lv_obj_get_coords(hc_slider(player_screen), &after);
		printf("CHAIN_LAYOUT seek_y_before=%d after=%d\n", before.y1, after.y1);
		capture("chain-after-source-details");
		assert(before.y1 == after.y1 && before.y2 == after.y2);
		puts("UI_TEST_PASS chain: progress bounds stay stable while unknown source details become known");
		fflush(stdout);
		exit(0);
	}
	}
}
