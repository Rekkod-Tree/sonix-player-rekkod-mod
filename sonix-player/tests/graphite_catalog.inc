// Query-free discovery exercised through actual LVGL controls and workers.
static void catalog_nav(const char *title) {
	lv_obj_t *label = spoken_exact_label(lv_screen_active(), title);
	assert(label && visible(label));
	lv_obj_scroll_to_view(lv_obj_get_parent(label), LV_ANIM_OFF);
	lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
}

static void catalog_row(const char *title) {
	lv_obj_t *label = find_label(lv_screen_active(), title);
	assert(label && visible(label));
	lv_area_t box;
	lv_obj_get_coords(label, &box);
	input(240, (box.y1 + box.y2) / 2, true);
	input(240, (box.y1 + box.y2) / 2, false);
}

static void catalog_mode(const char *mode) {
	FILE *f = fopen(getenv("SONIX_CATALOG_MOCK_MODE"), "w");
	assert(f && fprintf(f, "%s\n", mode) > 0);
	assert(fclose(f) == 0);
}

static void catalog_states(void) {
	static int step;
	static bool light;
	const char *book = "Симона де Б";
	switch (step) {
	case 0: {
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		theme_set_dark(true);
		char message[256];
		assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		break;
	}
	case 1:
		switch_screen(yandex_screen);
		step = 101;
		return;
	case 101:
		capture(light ? "catalog-yandex-home-light" : "catalog-yandex-home-dark");
		catalog_nav(tr("audiobooks"));
		step = 2;
		return;
	case 2:
		if (!spoken_wait_label("Book chart"))
			return;
		assert(find_label(lv_screen_active(), "Fiction"));
		capture(light ? "catalog-yandex-books-light" : "catalog-yandex-books-dark");
		catalog_row("Book chart");
		break;
	case 3:
		if (!spoken_wait_label(book))
			return;
		capture(light ? "catalog-yandex-book-list-light" : "catalog-yandex-book-list-dark");
		catalog_row(book);
		break;
	case 4: {
		char parts[80];
		snprintf(parts, sizeof(parts), tr("spoken_chapter_count"), 13);
		if (!spoken_wait_label(parts))
			return;
		assert(visible(find_label(lv_screen_active(), tr("spoken_download_book"))));
		capture(light ? "catalog-yandex-book-detail-light" : "catalog-yandex-book-detail-dark");
		back_btn_cb(NULL);
		break;
	}
	case 5:
		if (!spoken_wait_label(book))
			return;
		back_btn_cb(NULL);
		break;
	case 6:
		if (!spoken_wait_label("Fiction"))
			return;
		catalog_row("Fiction");
		break;
	case 7:
		if (!spoken_wait_label("Book chart"))
			return;
		catalog_row("Book chart");
		break;
	case 8:
		if (!spoken_wait_label(book))
			return;
		switch_screen(yandex_screen);
		catalog_nav(tr("svc_browse_picks"));
		break;
	case 9:
		if (!spoken_wait_label("Road Trip"))
			return;
		capture(light ? "catalog-yandex-picks-light" : "catalog-yandex-picks-dark");
		catalog_row("Road Trip");
		break;
	case 10:
		if (!spoken_wait_label("MP3 Sine"))
			return;
		back_btn_cb(NULL);
		break;
	case 11:
		if (!spoken_wait_label("Road Trip"))
			return;
		switch_screen(zvuk_screen);
		step = 111;
		return;
	case 111:
		capture(light ? "catalog-zvuk-home-light" : "catalog-zvuk-home-dark");
		catalog_nav(tr("svc_browse_picks"));
		step = 12;
		return;
	case 12:
		if (!spoken_wait_label("Mock mix"))
			return;
		capture(light ? "catalog-zvuk-picks-light" : "catalog-zvuk-picks-dark");
		catalog_row("Mock mix");
		break;
	case 13:
		if (!spoken_wait_label("Breathe"))
			return;
		switch_screen(zvuk_screen);
		catalog_nav(tr("audiobooks"));
		break;
	case 14:
		if (!spoken_wait_label("Fixture Complete"))
			return;
		capture(light ? "catalog-zvuk-books-light" : "catalog-zvuk-books-dark");
		catalog_row("Fixture Complete");
		break;
	case 15:
		if (!spoken_wait_label("Chapter 01"))
			return;
		assert(visible(find_label(lv_screen_active(), tr("spoken_download_book"))));
		capture(light ? "catalog-zvuk-book-detail-light" : "catalog-zvuk-book-detail-dark");
		back_btn_cb(NULL);
		break;
	case 16: {
		if (!spoken_wait_label("Fixture Complete"))
			return;
		lv_obj_t *scroll = lv_obj_get_parent(find_label(lv_screen_active(), "Fixture Complete"));
		while (scroll && lv_obj_get_scroll_bottom(scroll) < 1000)
			scroll = lv_obj_get_parent(scroll);
		assert(scroll);
		lv_obj_scroll_to_y(scroll, lv_obj_get_scroll_y(scroll) + lv_obj_get_scroll_bottom(scroll), LV_ANIM_OFF);
		break;
	}
	case 17:
		if (!spoken_wait_label(tr("svc_browse_next")))
			return;
		capture(light ? "catalog-zvuk-more-light" : "catalog-zvuk-more-dark");
		catalog_row(tr("svc_browse_next"));
		break;
	case 18:
		if (!spoken_wait_label("Browse book 21"))
			return;
		capture(light ? "catalog-zvuk-page2-light" : "catalog-zvuk-page2-dark");
		back_btn_cb(NULL);
		break;
	case 19:
		if (!spoken_wait_label("Fixture Complete"))
			return;
		catalog_nav(tr("svc_browse_children"));
		break;
	case 20:
		if (!spoken_wait_label("Children book 01"))
			return;
		capture(light ? "catalog-zvuk-children-light" : "catalog-zvuk-children-dark");
		catalog_mode("catalog-empty");
		catalog_nav(tr("svc_browse_recommended"));
		break;
	case 21:
		if (!spoken_wait_label(tr("zvuk_empty")))
			return;
		capture(light ? "catalog-empty-light" : "catalog-empty-dark");
		catalog_mode("500");
		catalog_nav(tr("svc_browse_children"));
		break;
	case 22:
		if (!spoken_wait_label(tr("stream_retry")))
			return;
		assert(!visible(find_label(lv_screen_active(), tr("svc_browse_children"))));
		capture(light ? "catalog-error-light" : "catalog-error-dark");
		catalog_mode("ok");
		catalog_nav(tr("stream_retry"));
		break;
	case 23:
		if (!spoken_wait_label("Children book 01"))
			return;
		assert(audio_get_status() != AUDIO_STATUS_PLAYING);
		if (!light) {
			light = true;
			theme_set_dark(false);
			step = 1;
			return;
		}
		puts("UI_TEST_PASS catalog: both services, genres, book detail, playlists, next/back, children, empty/error/retry, both themes, no autoplay");
		fflush(stdout);
		exit(0);
	}
	step++;
}
