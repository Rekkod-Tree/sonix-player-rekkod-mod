// Real component renders across every palette, in disposable host-only roots.
static void chrome_edges_states(void) {
	static int phase, variant;
	static bool paused;
	static uint32_t began;
	char shot[96];
	if (phase == 0) {
		gui_init_rest();
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		began = lv_tick_get();
		phase++;
		return;
	}
	if (phase == 1) {
		device_state_t state;
		device_state_get(&state);
		if (state.status != AUDIO_STATUS_PLAYING) {
			assert(lv_tick_elaps(began) < 12000);
			return;
		}
		device_state_toggle_play_pause();
		paused = true;
		switch_screen_no_history(main_menu_screen);
		phase++;
		return;
	}
	assert(paused);
	int family = variant < 18 ? variant / 2 : variant == 18 ? 0 : 5;
	bool dark = variant >= 18 || variant % 2;
	switch (phase++) {
	case 2:
		theme_set_palette(family);
		theme_set_dark(dark);
		theme_set_dynamic_tint(variant >= 18);
		theme_set_accent(family ? THEME_ACCENT_PALETTE : THEME_ACCENT_GRAPHITE);
		assert(sx_pal()->edge_frame == (family == 4 || family == 5));
		if (family == 3)
			assert(!sx_pal()->mini_edge_opa && !sx_pal()->floating_edge_opa && sx_pal()->surface_edge_opa && sx_pal()->shade_edge_opa);
		if (family == 0)
			assert(sx_pal()->surface_edge_opa && sx_pal()->mini_edge_opa && sx_pal()->shade_edge_opa < sx_pal()->mini_edge_opa);
		volume_overlay_show(40);
		sx_toast(tr("queue_history"));
		break;
	case 3:
		snprintf(shot, sizeof(shot), "edges-%02d-%s-chrome", variant, dark ? "dark" : "light");
		capture(shot);
		sx_toast_dismiss();
		break;
	case 4: {
		const sx_sheet_item_t items[] = {
			{.label = tr("queue_play_next"), .icon = &npicon_play_next},
			{.label = tr("show_album"), .icon = &icon_menu_album},
		};
		sx_sheet_menu("Time", "Pink Floyd", items, 2);
		break;
	}
	case 5:
		snprintf(shot, sizeof(shot), "edges-%02d-%s-sheet", variant, dark ? "dark" : "light");
		capture(shot);
		sx_sheet_close();
		break;
	case 6:
		quickpanel_open();
		break;
	case 7:
		snprintf(shot, sizeof(shot), "edges-%02d-%s-shade", variant, dark ? "dark" : "light");
		capture(shot);
		quickpanel_close();
		break;
	case 8:
		main_menu_open_search();
		break;
	case 9:
		snprintf(shot, sizeof(shot), "edges-%02d-%s-keyboard", variant, dark ? "dark" : "light");
		capture(shot);
		back_btn_cb(NULL);
		if (++variant < 20)
			phase = 2;
		else {
			puts("UI_TEST_PASS palette edge matrix:9 families light/dark,2 cover-tinted cases, mini/volume/toast/sheet/shade");
			fflush(stdout);
			exit(0);
		}
		break;
	}
}
