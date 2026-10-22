// Actual cached playlist content regression; run only with disposable roots.
static lv_obj_t *pc_lead, *pc_plus;
static unsigned pc_deletes;
static lv_color_t pc_before;
static const char *pc_name = "Cobalt mix";
static bool pc_dark;
static void pc_deleted(lv_event_t *e) {
	(void)e;
	pc_deletes++;
}
static lv_obj_t *pc_label(lv_obj_t *root, const char *text) {
	if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *r = pc_label(lv_obj_get_child(root, (int)i), text);
		if (r)
			return r;
	}
	return NULL;
}
static void pc_tap_option(const char *key) {
	lv_obj_t *label = pc_label(appearance_screen, tr(key));
	assert(label);
	lv_obj_scroll_to_view_recursive(lv_obj_get_parent(label), LV_ANIM_OFF);
	lv_obj_update_layout(appearance_screen);
	lv_area_t a;
	lv_obj_get_coords(label, &a);
	assert(a.x1 >= 0 && a.x2 < 480 && a.y1 >= 40 && a.y2 < lv_display_get_vertical_resolution(lv_display_get_default()));
	input((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, true);
	input((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, false);
}
static void pc_check(const char *state) {
	assert(!pc_deletes);
	assert(lv_obj_get_parent(pc_label(playlistpage_screen, "C")) == pc_lead);
	lv_color_t actual = lv_obj_get_style_bg_color(pc_lead, 0), expected = sx_placeholder_tone(sx_palette_hue_of_name(pc_name));
	bool mismatch = !lv_color_eq(actual, expected);
	printf("PLAYLIST_CONTENT state=%s dark=%d mono=%d force=%d actual=%06x expected=%06x cached_delete=%u mismatch=%d\n", state, pc_dark, theme_monochrome_covers(), theme_force_color_placeholders(), lv_color_to_u32(actual), lv_color_to_u32(expected), pc_deletes, mismatch);
	assert(!mismatch);
	assert(lv_color_eq(lv_obj_get_style_bg_color(pc_plus, 0), sx_pal()->accent));
	char name[96];
	snprintf(name, sizeof(name), "playlist-content-%s-%s", pc_dark ? "dark" : "light", state);
	capture(name);
}
static void playlist_content_states(lv_timer_t *timer) {
	static int step;
	switch (step++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_palette(0);
		theme_set_accent(0);
		assert(playlists_create(pc_name));
		break;
	case 1:
		theme_set_dark(pc_dark);
		theme_set_monochrome_covers(false);
		theme_set_force_color_placeholders(false);
		playlistpage_open();
		pc_deletes = 0;
		break;
	case 2: {
		lv_obj_t *letter = pc_label(playlistpage_screen, "C"), *plus = pc_label(playlistpage_screen, "+");
		assert(letter && plus);
		pc_lead = lv_obj_get_parent(letter);
		pc_plus = lv_obj_get_parent(plus);
		lv_obj_add_event_cb(pc_lead, pc_deleted, LV_EVENT_DELETE, NULL);
		pc_before = lv_obj_get_style_bg_color(pc_lead, 0);
		pc_check("color-before");
		switch_screen(appearance_screen);
		break;
	}
	case 3:
		pc_tap_option("palette_mono_covers");
		break;
	case 4:
		assert(theme_monochrome_covers());
		switch_screen(playlistpage_screen);
		break;
	case 5:
		pc_check("mono-return");
		switch_screen(appearance_screen);
		break;
	case 6:
		pc_tap_option("palette_force_color");
		break;
	case 7:
		assert(theme_force_color_placeholders());
		switch_screen(playlistpage_screen);
		break;
	case 8:
		pc_check("force-return");
		switch_screen(appearance_screen);
		break;
	case 9:
		pc_tap_option("palette_force_color");
		break;
	case 10:
		assert(!theme_force_color_placeholders());
		switch_screen(playlistpage_screen);
		break;
	case 11:
		pc_check("mono-again");
		break;
	case 12:
		theme_set_dark(!pc_dark);
		pc_check("theme-flip");
		break;
	case 13:
		theme_set_dark(pc_dark);
		pc_check("theme-back");
		switch_screen(appearance_screen);
		break;
	case 14:
		pc_tap_option("palette_mono_covers");
		break;
	case 15:
		assert(!theme_monochrome_covers());
		switch_screen(playlistpage_screen);
		break;
	case 16:
		pc_check("color-restored");
		assert(lv_color_eq(lv_obj_get_style_bg_color(pc_lead, 0), pc_before));
		printf("UI_TEST_PASS cached playlist content complete %s plus_stays_accent\n", pc_dark ? "dark" : "light");
		if (!pc_dark) {
			pc_dark = true;
			step = 1;
		} else {
			lv_timer_delete(timer);
			fflush(stdout);
			exit(0);
		}
		break;
	}
}
