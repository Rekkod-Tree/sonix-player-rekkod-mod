#include "src/gui/shell/artview.h"

static unsigned motion_style_invalidations;
static void motion_style_invalidated(lv_event_t *event) {
	(void)event;
	motion_style_invalidations++;
}

static void motion_artwork_policy(void) {
	lv_obj_t *previous = lv_screen_active();
	lv_obj_t *screen = lv_obj_create(NULL);
	lv_screen_load(screen);
	// A stable background must not invalidate the whole player on every track.
	lv_color_t background = lv_color_hex(0x182124);
	sx_set_bg_color(screen, background);
	lv_style_value_t local;
	assert(lv_obj_get_local_style_prop(screen, LV_STYLE_BG_COLOR, &local, 0) == LV_STYLE_RES_FOUND);
	assert(lv_color_eq(local.color, background));
	lv_refr_now(NULL);
	lv_display_t *display = lv_display_get_default();
	lv_display_add_event_cb(display, motion_style_invalidated, LV_EVENT_INVALIDATE_AREA, NULL);
	motion_style_invalidations = 0;
	for (int i = 0; i < 100; i++)
		sx_set_bg_color(screen, background);
	assert(motion_style_invalidations == 0);
	sx_set_bg_color(screen, lv_color_hex(0x253034));
	assert(motion_style_invalidations > 0);
	lv_display_remove_event_cb_with_user_data(display, motion_style_invalidated, NULL);
	puts("UI_STATIC_BACKGROUND_PASS 100 unchanged refreshes without invalidation; changed role repaints");
	lv_obj_t *overlay = lv_obj_create(lv_layer_top());
	int height = lv_display_get_vertical_resolution(lv_display_get_default());
	lv_obj_set_size(overlay, 480, height);
	lv_obj_set_y(overlay, height);
	uint16_t pixels[16 * 16];
	for (unsigned i = 0; i < 16 * 16; i++)
		pixels[i] = i % 2 ? 0xf800 : 0x07e0;
	lv_image_dsc_t source = {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 16, .h = 16, .stride = 32}, .data_size = sizeof(pixels), .data = (void *)pixels};
	lv_obj_t *under = lv_image_create(screen), *over = lv_image_create(overlay);
	lv_obj_set_pos(under, 40, 40);
	lv_obj_set_pos(over, 40, 40);
	assert(sx_artview_bind(under, &source, cover_new_display_generation()));
	assert(sx_artview_bind(over, &source, cover_new_display_generation()));
	lv_obj_update_layout(screen);
	lv_obj_update_layout(lv_layer_top());
	sx_artview_opaque_overlay(NULL);
	assert(sx_artview_mode(true));
	sx_artview_overlay_will_show(overlay);
	assert(((const lv_image_dsc_t *)lv_image_get_src(over))->data != source.data);
	lv_obj_set_y(overlay, 0);
	sx_artview_opaque_overlay(overlay);
	assert(((const lv_image_dsc_t *)lv_image_get_src(under))->data == source.data);
	// Closing/dragging a previously opaque player prepares both surfaces once.
	sx_artview_overlay_will_show(overlay);
	assert(((const lv_image_dsc_t *)lv_image_get_src(under))->data != source.data);
	const void *kept = ((const lv_image_dsc_t *)lv_image_get_src(over))->data;
	unsigned conversions = sx_artview_stats().conversions;
	for (int i = 0; i < 50; i++) {
		lv_obj_set_y(overlay, height - i * 4);
		sx_artview_overlay_will_show(overlay);
		assert(((const lv_image_dsc_t *)lv_image_get_src(over))->data == kept);
		assert(sx_artview_stats().conversions == conversions);
	}
	lv_obj_set_hidden(overlay, true);
	sx_artview_opaque_overlay(NULL);
	lv_obj_delete(overlay);
	lv_screen_load(previous);
	lv_obj_delete(screen);
	assert(sx_artview_mode(false));
	puts("UI_MOTION_ARTWORK_PASS both surfaces retained during 50 held positions, no repeated conversions");
}

static lv_obj_t *motion_art(lv_obj_t *root) {
	int w = lv_obj_get_width(root);
	if (w > 200 && w < 480 && lv_obj_get_height(root) == w)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = motion_art(lv_obj_get_child(root, (int)i));
		if (found)
			return found;
	}
	return NULL;
}

static void motion_states(lv_timer_t *timer) {
	static int step, waiting, mini_y, height;
	static unsigned revision;
	device_state_t state;
	device_state_get(&state);
	switch (step++) {
	case 0:
		motion_artwork_policy();
		hc_setup(timer);
		break;
	case 1: {
		if (library_scan_running()) {
			step--;
			break;
		}
		const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT")};
		playlist_set_mode(PLAYBACK_MODE_NORMAL);
		device_state_play_list(paths, 2, 0);
		player_refresh_now_playing();
		switch_screen_no_history(music_screen);
		coverflow_set_enabled(true);
		break;
	}
	case 2:
		if (state.status != AUDIO_STATUS_PLAYING) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		player_sheet_close(false);
		lv_obj_update_layout(lv_layer_top());
		height = lv_display_get_vertical_resolution(lv_display_get_default());
		mini_y = height - 40;
		revision = playlist_revision();
		input(240, mini_y, true);
		input(240, mini_y - 80, true);
		break;
	case 3:
		assert(player_sheet_is_open() && player_sheet_drag_active());
		assert(lv_obj_get_y(player_screen) > 0 && lv_obj_get_y(player_screen) < height - 80);
		capture("motion-mini-player-pull-held");
		input(240, mini_y - 260, true);
		input(240, mini_y - 260, false);
		break;
	case 4:
		assert(player_sheet_is_open() && lv_obj_get_y(player_screen) == 0);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")) && playlist_revision() == revision);
		input(71, 250, true);
		input(220, 250, true);
		break;
	case 5:
		assert(player_sheet_is_open() && lv_obj_get_x(player_screen) > 0);
		capture("motion-player-edge-back-held");
		input(220, 250, false);
		break;
	case 6:
		assert(!player_sheet_is_open() && lv_obj_get_x(player_screen) == 0 && sx_mini_visible());
		assert(lv_screen_active() == music_screen && !strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		input(240, mini_y, true);
		input(240, mini_y - 20, true);
		input(240, mini_y - 20, false);
		break;
	case 7:
		assert(!player_sheet_is_open() && sx_mini_visible());
		player_sheet_open(false);
		lv_obj_update_layout(player_screen);
		input(240, 220, true);
		input(120, 220, true);
		break;
	case 8:
		assert(motion_art(player_screen) && lv_obj_get_style_translate_x(motion_art(player_screen), 0) == -120);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		capture("motion-cover-swipe-held");
		input(120, 220, false);
		waiting = 0;
		break;
	case 9:
		if (strcmp(state.current_file, getenv("SONIX_UI_TEST_NEXT")) || state.status != AUDIO_STATUS_PLAYING) {
			assert(waiting++ < 40);
			step--;
			break;
		}
		assert(lv_obj_get_style_translate_x(motion_art(player_screen), 0) == 0);
		capture("motion-cover-next-settled");
		input(71, 250, true);
		input(100, 250, true);
		break;
	case 10:
		input(100, 250, false);
		break;
	case 11:
		assert(player_sheet_is_open() && lv_obj_get_x(player_screen) == 0);
		player_sheet_close(false);
		qe_click(find_image(music_screen, &icon_ellipsis_vertical));
		break;
	case 12:
		assert(sx_sheet_is_open() && visible(find_label(lv_layer_top(), tr("music_album_covers"))));
		capture("motion-explicit-album-covers-action");
		qe_click(find_label(lv_layer_top(), tr("music_album_covers")));
		break;
	case 13:
		assert(!sx_sheet_is_open() && !player_sheet_is_open());
		capture("motion-explicit-album-covers-opened");
		puts("UI_TEST_PASS motion: held mini pull, commit/cancel, player edge Back/cancel, cover drag/Next, explicit album covers");
		fflush(stdout);
		exit(0);
	}
}
