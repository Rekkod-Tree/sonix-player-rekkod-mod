// Ordinary application UI regression; the disposable runner owns all data.
static void search_scope_states(void) {
	static int step;
	static bool dark;
	static lv_obj_t *field;
	static int32_t saved_cursor;
	static bool saved_focus;
	char image_name[96];
	switch (step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_dark(dark);
		switch_screen_no_history(main_menu_screen);
		break;
	case 1:
		main_menu_open_search();
		break;
	case 2:
		assert(sx_sheet_is_open());
		qe_click(find_label(lv_layer_top(), tr("home_search_card")));
		break;
	case 3:
		assert(lv_screen_active() == search_screen);
		field = find_field(search_screen);
		assert(field);
		lv_textarea_set_text(field, "scope retained");
		snprintf(image_name, sizeof(image_name), "search-scope-%s-entry", dark ? "dark" : "light");
		capture(image_name);
		switch_screen_no_history(main_menu_screen);
		break;
	case 4:
		main_menu_open_search();
		break;
	case 5:
		assert(lv_screen_active() == search_screen && !sx_sheet_is_open());
		assert(!strcmp(lv_textarea_get_text(field), "scope retained"));
		saved_cursor = lv_textarea_get_cursor_pos(field);
		saved_focus = lv_obj_has_state(field, LV_STATE_FOCUSED);
		qe_click(find_label(search_screen, tr("home_search_card")));
		break;
	case 6:
		assert(sx_sheet_is_open());
		snprintf(image_name, sizeof(image_name), "search-scope-%s-choose", dark ? "dark" : "light");
		capture(image_name);
		sx_sheet_close();
		break;
	case 7:
		assert(!sx_sheet_is_open() && lv_screen_active() == search_screen);
		assert(lv_textarea_get_cursor_pos(field) == saved_cursor);
		assert(lv_obj_has_state(field, LV_STATE_FOCUSED) == saved_focus);
		assert(!strcmp(lv_textarea_get_text(field), "scope retained"));
		printf("UI_TEST_PASS search source first choice, direct repeat, visible switch, dismiss preserves query %s\n", dark ? "dark" : "light");
		if (!dark) {
			dark = true;
			theme_set_dark(true);
			step = 3;
		} else {
			fflush(stdout);
			exit(0);
		}
		break;
	}
}
