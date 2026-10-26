// Full application coverage with disposable media/config and the native UI.
static lv_obj_t *ux_result(lv_obj_t *root, const char *title) {
	if (lv_obj_check_type(root, &lv_label_class) &&
		lv_obj_get_style_text_font(root, 0) == &font_ui_26_bold &&
		!strncmp(lv_label_get_text(root), title, strlen(title)) && visible(root)) return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = ux_result(lv_obj_get_child(root, (int)i), title);
		if (found) return found;
	}
	return NULL;
}

static lv_obj_t *ux_filter_label(lv_obj_t *root,const char *text) {
 if(!visible(root)) return NULL;
 if(lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text) && lv_obj_check_type(lv_obj_get_parent(root),&lv_button_class)) return root;
 for(uint32_t i=0;i<lv_obj_get_child_count(root);i++) {
  lv_obj_t *found=ux_filter_label(lv_obj_get_child(root,(int)i),text);if(found) return found;
 }
 return NULL;
}
static void ux_filter_pick(lv_obj_t *root,const char *key) {
 lv_obj_t *label=ux_filter_label(root,tr(key));assert(label);
 lv_obj_scroll_to_view(lv_obj_get_parent(label),LV_ANIM_OFF);
 lv_obj_update_layout(root);lv_refr_now(NULL);qe_click(label);
}

static bool ux_open_source(void) {
	if (!sx_sheet_is_open()) { trackmenu_open(NULL); return false; }
	capture("ux-source-menu");
	qe_click(find_label(lv_layer_top(), tr("playback_source_open")));
	return true;
}

static void ux_seed_cached_track(const char *id) {
	char path[512];
	zvukcache_path(id, "flac", path, sizeof(path));
	const char *fixture = getenv("SONIX_UI_TEST_STREAM_TRACK"); assert(fixture);
	FILE *in = fopen(fixture, "rb"), *out = fopen(path, "wb");
	assert(in && out);
	char block[4096]; size_t n;
	while ((n = fread(block, 1, sizeof(block), in))) assert(fwrite(block, 1, n, out) == n);
	assert(!ferror(in)); assert(!fclose(in)); assert(!fclose(out));
}

static void ux_mock_mode(const char *mode) {
	FILE *f = fopen(getenv("SONIX_UI_TEST_MOCK_MODE"), "w");
	assert(f && fprintf(f, "%s\n", mode) > 0); assert(!fclose(f));
}

static int ux_search_workers(void) {
	DIR *directory = opendir("/proc/self/task"); assert(directory);
	struct dirent *entry; int count = 0;
	while ((entry = readdir(directory))) {
		char path[96], name[64];
		snprintf(path, sizeof(path), "/proc/self/task/%.32s/comm", entry->d_name);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		if (fgets(name, sizeof(name), f) && !strncmp(name, "search-", 7)) count++;
		fclose(f);
	}
	closedir(directory); return count;
}

static void ux_delay_mode(const char *mode) {
	FILE *f = fopen(getenv("SONIX_UI_TEST_YANDEX_DELAY"), "w");
	assert(f && fprintf(f, "%s\n", mode) > 0); assert(!fclose(f));
}

static int ux_label_count(lv_obj_t *root, const char *text) {
	int count = lv_obj_check_type(root, &lv_label_class) && visible(root) && !strcmp(lv_label_get_text(root), text);
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) count += ux_label_count(lv_obj_get_child(root, (int)i), text);
	return count;
}

static void ux_search_submit(lv_obj_t *root, const char *query) {
	uint32_t before = lv_tick_get();
	lv_obj_t *field = find_field(root); assert(field);
	qe_click(field);
	lv_textarea_set_text(field, query);
	lv_refr_now(NULL); // The keyboard must be drawn before its Search key can be tapped.
	qe_click(find_visible_image(root, &icon_search));
	uint32_t elapsed = lv_tick_elaps(before);
	printf("UX_SEARCH_SUBMIT_MS %u\n", elapsed);
	assert(elapsed < 500); // Network latency must never be paid on the GUI thread.
}

static void ux_network_states(void) {
	static int step;
	static lv_obj_t *all;
	static uint32_t started;
	switch (step++) {
	case 0: {
		gui_init_rest();
		pointer = lv_indev_create(); lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		assert(yandex_configured() && yandex_logged_in());
		char message[256];
		assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	}
	case 1:
		if (library_scan_running()) { step--; break; }
		main_menu_open_search();
		break;
	case 2:
		all = lv_screen_active();
		ux_search_submit(all, "Pink Floyd"); started = lv_tick_get();
		break;
	case 3:
		assert(ux_result(all, "Time") && lv_tick_elaps(started) < 4000);
		capture("ux-network-local-before-slow-service");
		ux_search_submit(all, "Speak"); started = lv_tick_get();
		break;
	case 4:
		if (!ux_result(all, "Speak to Me") && lv_tick_elaps(started) < 4000) { step--; break; }
		assert(ux_result(all, "Speak to Me") && lv_tick_elaps(started) < 4000);
		assert(visible(find_label(all, tr("loading"))) && ux_search_workers() > 0);
		capture("ux-network-fast-service-before-slow-service");
		qe_click(find_visible_image(all, &icon_clear));
		break;
	case 5:
		if (lv_tick_elaps(started) < 7000) { step--; break; }
		assert(!ux_result(all, "Mock Album"));
		assert(!visible(find_label(all, tr("lists_filter_all"))));
		ux_delay_mode("offline"); ux_mock_mode("offline");
		ux_search_submit(all, "Pink Floyd"); started = lv_tick_get();
		break;
	case 6:
		assert(ux_result(all, "Time") && lv_tick_elaps(started) < 4000);
		ux_filter_pick(all, "artists");
		capture("ux-network-offline-local-results");
		break;
	case 7:
		if (ux_label_count(all, tr("search_source_retry")) < 2 && lv_tick_elaps(started) < 12000) { step--; break; }
		assert(ux_label_count(all, tr("search_source_retry")) == 2);
		assert(ux_result(all, "Pink Floyd") && !ux_result(all, "Mock Album"));
		capture("ux-network-offline-independent-errors");
        ux_mock_mode("slow-api");
        qe_click(ux_result(all,tr("search_source_retry"))); started=lv_tick_get();
        break;
    case 8:
        if(ux_label_count(all,tr("search_source_retry"))<2 && lv_tick_elaps(started)<4000) {step--;break;}
        assert(ux_label_count(all,tr("search_source_retry"))==2 && !ux_search_workers());
        capture("ux-network-targeted-retry-no-zvuk-restart");
        ux_mock_mode("ok");ux_delay_mode("0");
        qe_click(ux_result(all,tr("search_source_retry")));started=lv_tick_get();
        break;
    case 9:
        if(!ux_result(all,"Mock Artist") && lv_tick_elaps(started)<4000) {step--;break;}
        assert(ux_result(all,"Mock Artist") && ux_result(all,"Pink Floyd"));
        assert(ux_label_count(all,tr("search_source_retry"))==1);
        capture("ux-network-targeted-retry-recovered-one-source");
        yandex_logout();zvuk_logout();
        ux_search_submit(all,"Pink Floyd");ux_filter_pick(all,"lists_filter_all");
        break;
    case 10:
        assert(ux_result(all,"Time") && !ux_search_workers());
        capture("ux-network-no-accounts-local-results");
        puts("UI_TEST_PASS independent slow/fast service search, stale cancellation, offline, targeted retry and no-account idle");
        fflush(stdout);exit(0);
	}
}

// All account data and API replies in this scenario are disposable fixtures.
static void ux_sources_states(void) {
 static int step,index;
 static lv_obj_t *all;
 static uint32_t started;
 static const char *names[]={"tidal","qobuz","subsonic","podcasts"};
 static const char *hits[]={"Tidal Track","Qobuz Track","Server Track","Podcast Show"};
 switch(step++) {
 case 0: {
  gui_init_rest();
  pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(pointer,read_pointer);
  char message[256];assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90",message,sizeof(message))==CARDIMPORT_OK);
  assert(search_source_count()==6);
  for(int i=0;i<search_source_count();i++) assert(search_source_at(i)->ready());
  assert(library_scan_start(getenv("SONIX_SD_ROOT")));break;
 }
 case 1:
  if(library_scan_running()) {step--;break;}
  main_menu_open_search();all=lv_screen_active();break;
 case 2:
  ux_search_submit(all,"Pink Floyd");started=lv_tick_get();break;
 case 3:
  if(ux_search_workers() && lv_tick_elaps(started)<6000) {step--;break;}
  assert(!ux_search_workers() && ux_result(all,"Time"));
  ux_filter_pick(all,"podcasts");break;
 case 4:
  assert(ux_result(all,"Podcast Show") && !ux_result(all,"Time"));
  capture("ux-all-sources-podcast-filter");
  break;
 case 5:
  qe_click(find_label(all,tr(index?names[index-1]:"search_everywhere")));break;
 case 6: {
  for(int i=0;i<4;i++) assert(find_label(lv_layer_top(),tr(names[i])));
  if(!index) capture("ux-search-connected-sources-picker");
  lv_obj_t *label=find_label(lv_layer_top(),tr(names[index]));assert(label);
  lv_obj_scroll_to_view(lv_obj_get_parent(label),LV_ANIM_OFF);lv_refr_now(NULL);qe_click(label);
  break;
 }
 case 7:
  assert(lv_screen_active()==all && find_label(all,tr(names[index])));
  assert(!strcmp(lv_textarea_get_text(find_field(all)),"Pink Floyd"));
  assert(!visible(find_label(all,tr("lists_filter_all"))));
  ux_search_submit(all,"Pink Floyd");started=lv_tick_get();break;
 case 8:
  if(!ux_result(all,hits[index]) && lv_tick_elaps(started)<6000) {step--;break;}
  assert(ux_result(all,hits[index]) && !ux_result(all,"Time"));
  {lv_area_t pill;lv_obj_get_coords(find_label(all,tr("lists_filter_all")),&pill);assert(pill.x1>=0 && pill.x2<480);}
  for(int i=0;i<4;i++) if(i!=index) assert(!ux_result(all,hits[i]));
  {char shot[80];snprintf(shot,sizeof(shot),"ux-scope-%s",names[index]);capture(shot);}
  if(index<3) {
   const char *albums[]={"Tidal Album","Qobuz Album","Server Album"};
   qe_click(ux_result(all,albums[index]));
  } else qe_click(ux_result(all,"Podcast Show"));
  started=lv_tick_get();break;
 case 9:
  if(lv_screen_active()==all && lv_tick_elaps(started)<6000) {step--;break;}
  assert(lv_screen_active()!=all);
  {char shot[80];snprintf(shot,sizeof(shot),"ux-delegate-open-%s",names[index]);capture(shot);}
  back_btn_cb(NULL);break;
 case 10:
  assert(lv_screen_active()==all && ux_result(all,hits[index]));
  if(++index<4) step=5;
  else {puts("UI_TEST_PASS six clients, connected picker, delegated scopes/actions/Back, shared query and podcast isolation");fflush(stdout);exit(0);}
  break;
 }
}

static void ux_context_states(void) {
	static int step;
	static bool dark;
	static lv_obj_t *all_screen, *field;
	static unsigned queue_revision;
	static int history_count;
	static int filter_check;
	static int browse_check;
	static bool source_check, service_browse_check;
	static lv_obj_t *browse_view;
	static int browse_y;
	static char music_before_book[320];
	static playbackplace_t music_place_before_book;
	static int zvuk_header_step;
	char shot[96];
	library_history_item_t *events = NULL;
	switch (step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_dark(false);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		break;
	case 1:
		if (library_scan_running()) { step--; break; }
		for (int i = 0; i < stream_provider_count(); i++) {
			const stream_provider_t *provider = stream_provider_at(i);
			const char *dir = provider->cache_dir ? provider->cache_dir() : NULL;
			if (!dir || !dir[0]) continue;
			char path[512], caption[384];
			snprintf(path, sizeof(path), "%s/%s", dir, !strcmp(provider->id, "spotify") ? "1234567890123456789012/audio.ogg" : "12345.flac");
			assert(!strcmp(svcui_track_source(path), tr(provider->id)));
			svcui_track_caption(path, "Artist", caption, sizeof(caption));
			assert(!strncmp(caption, tr(provider->id), strlen(tr(provider->id))));
			assert(strstr(caption, " · Artist"));
		}
		switch_screen(music_screen);
		search_open();
		break;
	case 2:
		field = find_field(search_screen); assert(field);
		lv_textarea_set_text(field, "Pink Floyd");
		qe_click(find_label(search_screen, tr("home_search_card")));
		break;
	case 3:
		assert(sx_sheet_is_open());
		qe_click(find_label(lv_layer_top(), tr("search_everywhere")));
		break;
	case 4:
		all_screen = lv_screen_active();
		assert(all_screen != search_screen);
		field = find_field(all_screen); assert(field);
		assert(!strcmp(lv_textarea_get_text(field), "Pink Floyd"));
		assert(!visible(find_label(all_screen, tr("lists_filter_all"))));
		capture("ux-all-query-cold-source");
		back_btn_cb(NULL);
		break;
	case 5:
		assert(lv_screen_active() == music_screen); // Source choice adds no Back step.
		main_menu_open_search();
		break;
	case 6:
		assert(lv_screen_active() == all_screen);
		assert(!strcmp(lv_textarea_get_text(field), "Pink Floyd"));
		qe_click(find_visible_image(all_screen, &icon_search)); // The keyboard's Search key.
		break;
	case 7:
		assert(visible(find_label(all_screen, tr("lists_filter_all"))));
		if (filter_check == 0) {
			assert(ux_result(all_screen, "Time"));
			ux_filter_pick(all_screen, "albums");
			filter_check++; step--; break;
		}
		if (filter_check == 1) {
			capture("ux-all-filter-albums");
			assert(!ux_result(all_screen, "Time"));
			assert(ux_result(all_screen, "The Dark Side of the"));
			ux_filter_pick(all_screen, "artists");
			filter_check++; step--; break;
		}
		if (filter_check == 2) {
			assert(!ux_result(all_screen, "Time"));
			assert(!ux_result(all_screen, "The Dark Side of the"));
			assert(ux_result(all_screen, "Pink Floyd"));
			capture("ux-all-filter-artists");
			ux_filter_pick(all_screen, "lists_filter_all");
			filter_check++; step--; break;
		}
		assert(visible(find_label(all_screen, "Time")));
		assert(visible(find_label(all_screen, "Echoes")));
		assert(!keyboard_shown_on(all_screen));
		snprintf(shot, sizeof(shot), "ux-all-local-results-%s", dark ? "dark" : "light"); capture(shot);
		qe_click(find_label(all_screen, "Time"));
		break;
	case 8:
		if (!playhistory_current_id()) { step--; break; }
		assert(playlist_count() == 2);
		device_state_toggle_play_pause();
		history_count = playhistory_items(&events); free(events);
		assert(history_count > 0);
		medialist_open("Browse B", LIBRARY_LIST_TRACKS, LIBRARY_FILTER_NONE, NULL);
		break;
	case 9: {
		if (browse_check == 0) {
			browse_view = ux_result(lv_screen_active(), "Artwork check"); assert(browse_view);
			lv_obj_update_layout(lv_screen_active());
			while (browse_view && (!lv_obj_is_scrollable(browse_view) || lv_obj_get_scroll_bottom(browse_view) < 240)) browse_view = lv_obj_get_parent(browse_view);
			assert(browse_view); lv_obj_scroll_to_y(browse_view, 240, LV_ANIM_OFF);
			browse_y = lv_obj_get_scroll_y(browse_view); assert(browse_y > 100);
			queue_revision = playlist_revision();
			switch_screen(settings_screen); browse_check++; step--; break;
		}
		if (browse_check == 1) {
			assert(lv_screen_active() == settings_screen);
			back_btn_cb(NULL); browse_check++; step--; break;
		}
		if (browse_check == 2) {
			assert(lv_obj_get_scroll_y(browse_view) == browse_y);
			assert(visible(find_label(lv_screen_active(), "Browse B")));
			switch_screen(main_menu_screen); browse_check++; step--; break;
		}
		grid_entry_t entry;
		assert(main_menu_get_entry(HOME_MUSIC, &entry) && entry.action);
		entry.action();
		break;
	}
	case 10:
		assert(lv_screen_active() == medialist_tracks_screen);
		if (!source_check) {
			assert(visible(find_label(lv_screen_active(), "Browse B")));
			assert(lv_obj_get_scroll_y(browse_view) == browse_y);
			if (!sx_sheet_is_open()) capture("ux-last-card-browse-after-settings");
			if (!ux_open_source()) { step--; break; }
			source_check = true; step--; break;
		}
		assert(visible(find_label(medialist_tracks_screen, "Time")));
		assert(playlist_revision() == queue_revision);
		capture("ux-return-card-query");
		back_btn_cb(NULL);
		break;
	case 11:
		assert(lv_screen_active() == music_screen && !player_sheet_is_open());
		qe_click(find_image(music_screen, &npicon_queue));
		break;
	case 12:
		assert(lv_screen_active() == queue_screen && !player_sheet_is_open());
		assert(visible(find_label(queue_screen, "На карте · Pink Floyd")));
		capture("ux-queue-from-music");
		back_btn_cb(NULL);
		break;
	case 13:
		assert(lv_screen_active() == music_screen && !player_sheet_is_open());
		assert(playlist_revision() == queue_revision);
		historypage_open(NULL);
		break;
	case 14:
		assert(visible(find_label(lv_screen_active(), "Time")));
		assert(visible(find_label(lv_screen_active(), tr("history_now"))));
		assert(visible(find_label(lv_screen_active(), "На карте · Pink Floyd")));
		assert(playhistory_items(&events) == history_count); free(events);
		capture("ux-history-confirmed-paused");
		device_state_toggle_play_pause();
		break;
	case 15:
		assert(playhistory_items(&events) == history_count); free(events);
		device_state_toggle_play_pause();
		switch_screen(main_menu_screen);
		main_menu_open_search();
		break;
	case 16:
		assert(lv_screen_active() == all_screen);
		qe_click(find_label(all_screen, tr("search_everywhere")));
		break;
	case 17:
		qe_click(find_label(lv_layer_top(), tr("home_search_card")));
		break;
	case 18:
		assert(lv_screen_active() == search_screen);
		assert(!strcmp(lv_textarea_get_text(find_field(search_screen)), "Pink Floyd"));
		qe_click(find_label(search_screen, tr("home_search_card")));
		break;
	case 19:
		qe_click(find_label(lv_layer_top(), tr("search_everywhere")));
		break;
	case 20:
		assert(lv_screen_active() == all_screen);
		assert(!strcmp(lv_textarea_get_text(find_field(all_screen)), "Pink Floyd"));
		capture("ux-query-shared-back-to-all");
		qe_click(find_visible_image(all_screen, &icon_clear));
		break;
	case 21:
		assert(!lv_textarea_get_text(find_field(all_screen))[0]);
		assert(visible(find_label(all_screen, "Pink Floyd")));
		assert(keyboard_shown_on(all_screen));
		assert(!visible(find_label(all_screen, tr("lists_filter_all"))));
		assert(visible(find_visible_image(all_screen, &icon_radio_recent)));
		capture("ux-search-recent-queries");
		lv_textarea_set_text(find_field(all_screen), "Pink Floyd");
		qe_click(find_visible_image(all_screen, &icon_search));
		break;
	case 22:
		theme_set_dark(true);
		capture("ux-all-local-results-dark");
		sx_mini_set_mode(SX_MINI_COMPACT);
		switch_screen(main_menu_screen);
		break;
	case 23:
		assert(sx_mini_slot_height() == SX_MINI_SLOT_H);
		capture("ux-home-normal-mini");
		switch_screen(music_screen);
		break;
	case 24:
		assert(sx_mini_slot_height() == SX_MINI_COMPACT_SLOT_H);
		capture("ux-section-compact-mini");
		break;
	case 25: {
		if (zvuk_header_step == 0) {
			char message[256];
			assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
			ux_seed_cached_track("1008");
			switch_screen(zvuk_screen);
			zvuk_header_step++;
			step--;
			break;
		}
		if (zvuk_header_step == 1) {
			lv_obj_t *search = find_visible_image(zvuk_screen, &icon_search);
			lv_obj_t *queue = find_visible_image(zvuk_screen, &npicon_queue);
			assert(search && queue);
			lv_area_t a, b;
			lv_obj_get_coords(lv_obj_get_parent(search), &a);
			lv_obj_get_coords(lv_obj_get_parent(queue), &b);
			assert(b.x2 < a.x1 && a.x2 < 480 && b.x1 > 100);
			capture("ux-zvuk-home-header-separated");
			qe_click(queue);
			zvuk_header_step++;
			step--;
			break;
		}
		if (zvuk_header_step == 2) {
			assert(lv_screen_active() == queue_screen);
			back_btn_cb(NULL);
			zvuk_header_step++;
			step--;
			break;
		}
		if (zvuk_header_step == 3) {
			assert(lv_screen_active() == zvuk_screen);
			qe_click(find_visible_image(zvuk_screen, &icon_search));
			zvuk_header_step++;
			step--;
			break;
		}
		assert(lv_screen_active() != zvuk_screen && find_field(lv_screen_active()));
		capture("ux-zvuk-home-search-opens");
		main_menu_open_search();
		break;
	}
	case 26:
		assert(lv_screen_active() == all_screen);
		capture("ux-all-before-service-query");
		qe_click(field);
		lv_textarea_set_text(field, "Dogs");
		capture("ux-all-service-query-edit");
		qe_click(find_visible_image(all_screen, &icon_search));
		break;
	case 27:
		if (!ux_result(all_screen, "Dogs")) { step--; break; }
		capture("ux-all-zvuk-results");
		qe_click(ux_result(all_screen, "Dogs"));
		break;
	case 28: {
		playbackplace_t place;
		if (!playbackplace_get("zvuk", &place)) { step--; break; }
		assert(!strcmp(place.a, "Dogs"));
		assert(strstr(place.path, "/1008.flac"));
		device_state_toggle_play_pause();
		assert(playlist_count() == 1);
		queue_revision = playlist_revision();
		assert(zvukpage_open_album("701", "Browsing elsewhere"));
		break;
	}
	case 29:
		switch_screen(main_menu_screen);
		break;
	case 30: {
		grid_entry_t entry;
		assert(main_menu_get_entry(HOME_ZVUK, &entry) && entry.action);
		entry.action();
		break;
	}
	case 31:
		if (!service_browse_check) {
			if (!ux_result(lv_screen_active(), "Группа крови")) { step--; break; }
			assert(playlist_revision() == queue_revision);
			if (!sx_sheet_is_open()) capture("ux-last-zvuk-browse-after-home");
			if (!ux_open_source()) { step--; break; }
			service_browse_check = true; step--; break;
		}
		if (!ux_result(lv_screen_active(), "Dogs")) { step--; break; }
		assert(playlist_revision() == queue_revision);
		capture("ux-return-zvuk-query-after-browse");
		back_btn_cb(NULL);
		assert(lv_screen_active() == zvuk_screen && !player_sheet_is_open());
		historypage_open(NULL);
		break;
	case 32:
		assert(visible(find_label(lv_screen_active(), "Звук · Pink Floyd")));
		capture("ux-history-zvuk-source");
		main_menu_open_search();
		ux_mock_mode("500");
		break;
	case 33:
		assert(lv_screen_active() == all_screen);
		qe_click(field);
		lv_textarea_set_text(field, "Dogs");
		qe_click(find_visible_image(all_screen, &icon_search));
		break;
	case 34:
		if (!visible(find_label(all_screen, tr("search_source_retry")))) { step--; break; }
		assert(!ux_result(all_screen, "Dogs"));
		capture("ux-all-zvuk-error");
		ux_mock_mode("ok");
		qe_click(find_label(all_screen, tr("search_source_retry")));
		break;
	case 35:
		if (!ux_result(all_screen, "Dogs")) { step--; break; }
		capture("ux-all-zvuk-recovered");
		zvuk_logout();
		break;
	case 36:
		assert(!ux_result(all_screen, "Dogs"));
		assert(visible(find_label(all_screen, tr("search_source_unavailable"))));
		capture("ux-all-zvuk-logged-out");
		queue_revision = playlist_revision();
		switcher_browse_forget("music"); switch_screen(main_menu_screen);
		break;
	case 37: {
		grid_entry_t entry; assert(main_menu_get_entry(HOME_MUSIC, &entry) && entry.action); entry.action();
		break;
	}
	case 38:
		assert(ux_result(lv_screen_active(), "Time") && playlist_revision() == queue_revision);
		capture("ux-first-card-entry-origin-fallback");
		browser_open_dir_public(getenv("SONIX_SD_ROOT"));
		switch_screen(browser_screen);
		break;
	case 39: {
		grid_entry_t entry;
		assert(main_menu_get_entry(HOME_MUSIC, &entry) && entry.subtitle);
		entry.subtitle(music_before_book, sizeof(music_before_book));
		char expected[320];
		snprintf(expected, sizeof(expected), tr("home_return_to_place"), tr("music_browse"));
		assert(!strcmp(music_before_book, expected));
		assert(playbackplace_get("music", &music_place_before_book));
		char root[512];
		snprintf(root, sizeof(root), "%s/Audiobooks", getenv("SONIX_SD_ROOT"));
		assert(audiobookdb_scan_start(root));
		break;
	}
	case 40:
		if (audiobookdb_scan_running()) {
			step--;
			break;
		}
		assert(audiobookdb_count() > 0);
		audiobooks_play_at(getenv("SONIX_UI_TEST_BOOK"), getenv("SONIX_UI_TEST_BOOK"), 17);
		break;
	case 41: {
		device_state_t state;
		device_state_get(&state);
		if (state.status != AUDIO_STATUS_PLAYING || strcmp(state.current_file, getenv("SONIX_UI_TEST_BOOK"))) {
			step--;
			break;
		}
		assert(audiobook_is_playing());
		bookresume_note(&state);
		switch_screen(main_menu_screen);
		break;
	}
	case 42: {
		grid_entry_t entry;
		char text[320];
		assert(main_menu_get_entry(HOME_MUSIC, &entry) && entry.subtitle && entry.action);
		entry.subtitle(text, sizeof(text));
		assert(!strcmp(text, music_before_book));
		assert(visible(find_label(main_menu_screen, text)));
		playbackplace_t place;
		assert(playbackplace_get("music", &place) && !strcmp(place.path, music_place_before_book.path));
		char book[512], file[512];
		assert(audiobook_current_book(book, sizeof(book), file, sizeof(file)));
		assert(!strcmp(file, getenv("SONIX_UI_TEST_BOOK")));
		capture("ux-book-playing-home-music-return-label");
		entry.action();
		break;
	}
	case 43:
		assert(lv_screen_active() == browser_screen && audiobook_is_playing());
		capture("ux-book-playing-music-retains-files");
		audiobookdb_mark_finished(getenv("SONIX_UI_TEST_BOOK"));
		switch_screen(main_menu_screen);
		break;
	case 44: {
		char text[320];
		bookresume_description(text, sizeof(text));
		assert(!strcmp(text, tr("home_choose_book")));
		bookresume_play();
		break;
	}
	case 45:
		assert(lv_screen_active() == audiobooks_screen && !sx_sheet_is_open());
		assert(visible(find_label(audiobooks_screen, "Home resume")));
		capture("ux-finished-continue-opens-books");
		for (int i = 0; i < 5; i++) {
			char key[32];
			snprintf(key, sizeof(key), i ? "resume_book_%d" : "resume_book", i);
			config_set("home", key, "");
		}
		switch_screen(main_menu_screen);
		break;
	case 46:
		bookresume_play();
		break;
	case 47:
		assert(lv_screen_active() == audiobooks_screen && !sx_sheet_is_open());
		assert(visible(find_label(audiobooks_screen, "Home resume")));
		capture("ux-empty-continue-opens-books");
		switch_screen(zvuk_screen);
		break;
	case 48:
		assert(!zvuk_logged_in());
		assert(find_visible_image(zvuk_screen, &npicon_queue));
		assert(!find_visible_image(zvuk_screen, &icon_search));
		capture("ux-zvuk-home-signed-out-header");
		library_close();
		assert(!switcher_browse_open("music"));
		assert(!switcher_browse_open("zvuk"));
		printf("UI_TEST_PASS UX shared query, filters/history, real starts, pause dedup, last Card/Zvuk browsing, explicit source and initial fallback, scroll/Back, stale-card rejection, queue, Home/section mini, service retry/logout, book keeps Music destination and origin separate, empty/finished Continue opens books\n");
		fflush(stdout); exit(0);
	}
}
