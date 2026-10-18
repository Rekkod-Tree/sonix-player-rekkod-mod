// Native UI checks against disposable Yandex fixtures, never a user account.
static void quick_actions_states(void) {
	static int step, wait;
	static char wave_file[512];
	device_state_t state;
	grid_entry_t entry;
	switch (step++) {
	case 0: {
		gui_init_rest();
		theme_set_dark(false);
		assert(yandex_logged_in());
		assert(main_menu_available(HOME_YANDEX_WAVE) && main_menu_available(HOME_YANDEX_LIKED));
		config_set("home", "tiles", "yandex,yandex_wave,yandex_liked,files,continue_book");
		main_menu_reload();
		assert(main_menu_at(1) == HOME_YANDEX_WAVE && main_menu_at(2) == HOME_YANDEX_LIKED);
		assert(!main_menu_move(HOME_SEARCH, 0));
		int n = 0;
		for (int i = 0; i < quickpanel_in_use_count(); i++) n += quickpanel_in_use_at(i) == QP_BTN_SLEEP;
		assert(n == 1);
		char folder[640];
		snprintf(folder, sizeof(folder), "%s/.service-fixture", getenv("SONIX_SD_ROOT"));
		assert(!mkdir(folder, 0755));
		filespage_open();
		break;
	}
	case 1: {
		assert(!find_label(filespage_screen, ".service-fixture"));
		capture("files-service-hidden");
		lv_obj_t *icon = find_visible_image(filespage_screen, &icon_ellipsis_vertical);
		assert(icon);
		lv_obj_send_event(lv_obj_get_parent(icon), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 2: {
		lv_obj_t *label = find_label(lv_layer_top(), tr("files_show_service"));
		assert(label);
		lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 3:
		assert(find_label(filespage_screen, ".service-fixture"));
		assert(config_get_int("files", "show_service", 0) == 1);
		capture("files-service-visible");
		switch_screen_no_history(main_menu_screen);
		break;
	case 4: {
		capture("home-wave-and-liked-light");
		lv_obj_t *tile = find_label(main_menu_screen, tr("yandex"));
		assert(tile);
		lv_obj_send_event(lv_obj_get_parent(tile), LV_EVENT_LONG_PRESSED, NULL);
		break;
	}
	case 5:
		assert(find_label(lv_layer_top(), tr("yandex_my_wave")));
		assert(find_label(lv_layer_top(), tr("yandex_liked")));
		capture("yandex-hold-quick-actions-light");
		sx_sheet_close();
		theme_set_dark(true);
		break;
	case 6:
		capture("home-wave-and-liked-dark");
		assert(main_menu_get_entry(HOME_YANDEX_LIKED, &entry));
		entry.action();
		break;
	case 7:
		assert(find_label(lv_screen_active(), tr("yandex_liked")));
		assert(audio_get_status() != AUDIO_STATUS_PLAYING);
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		break;
	case 8:
		yandexpage_wave_open();
		break;
	case 9: {
		assert(find_label(lv_layer_top(), tr("yandex_wave_replace")));
		capture("wave-replace-confirm");
		lv_obj_t *label = find_label(lv_layer_top(), tr("cancel"));
		assert(label); lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 10:
		device_state_get(&state);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		yandexpage_wave_open();
		break;
	case 11: {
		lv_obj_t *label = find_label(lv_layer_top(), tr("medialist_play"));
		assert(label); lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 12:
		device_state_get(&state);
		if (state.status != AUDIO_STATUS_PLAYING || !state.current_file[0] || !strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK"))) { assert(wait++ < 20); step--; break; }
		snprintf(wave_file, sizeof(wave_file), "%s", state.current_file);
		yandexpage_wave_open();
		break;
	case 13:
		device_state_get(&state);
		printf("Wave reopen: player=%d expected=%s current=%s\n", player_sheet_is_open(), wave_file, state.current_file);
		fflush(stdout);
		assert(player_sheet_is_open() && !strcmp(wave_file, state.current_file));
		device_state_toggle_play_pause();
		yandexpage_wave_open();
		break;
	case 14:
		device_state_get(&state);
		assert(state.status == AUDIO_STATUS_PLAYING && !strcmp(wave_file, state.current_file));
		player_sleep_sheet_open();
		break;
	case 15: {
		assert(find_label(lv_layer_top(), tr("np_sleep_end_album")));
		char text[40]; snprintf(text, sizeof(text), tr("np_sleep_minutes"), 15);
		lv_obj_t *label = find_label(lv_layer_top(), text);
		assert(label); lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 16:
		assert(sleeptimer_enabled(SLEEPTIMER_AUDIOBOOK));
		assert(sleeptimer_running(SLEEPTIMER_MUSIC));
		quickpanel_open();
		break;
	case 17:
		capture("shared-sleep-countdown-dark");
		quickpanel_close();
		yandexpage_liked_open();
		break;
	case 18:
		device_state_get(&state);
		assert(!strcmp(state.current_file, wave_file));
		assert(sleeptimer_remaining(SLEEPTIMER_PODCAST) < 900);
		puts("UI_TEST_PASS quick actions: persisted shortcuts, hold menu, hidden files toggle, liked browsing, wave reuse/resume, shared sleep");
		fflush(stdout);
		exit(0);
	}
}
