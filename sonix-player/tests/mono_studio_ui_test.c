#include "src/gui/shell/artview.h"
#define graphite_ui_test_init cap_original_unused_init
#include "graphite_ui_test.c"
#undef graphite_ui_test_init
#include "src/gui/settings/screensaver.h"
static int cap_phase = -2;
static lv_obj_t *grid, *thumbs[13], *saver;
static uint16_t thumb_pixels[13][56 * 56];
static lv_image_dsc_t thumb_source[13];
static bool old_cap;
static size_t normal_bytes, saver_bytes, overlap_bytes;
static unsigned overlap_failures;
static lv_obj_t *bound(lv_obj_t *o, const lv_image_dsc_t *source) {
	if (lv_obj_is_visible(o) && sx_artview_source_is(o, source))
		return o;
	for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
		lv_obj_t *r = bound(lv_obj_get_child(o, i), source);
		if (r)
			return r;
	}
	return NULL;
}
static lv_obj_t *dimensions(lv_obj_t *o, int w, int h, bool background) {
	const void *source = background ? lv_obj_get_style_bg_image_src(o, 0) : lv_obj_check_type(o, &lv_image_class) ? lv_image_get_src(o) : NULL;
	if (source && lv_image_src_get_type(source) == LV_IMAGE_SRC_VARIABLE) {
		const lv_image_dsc_t *d = source;
		if (d->header.cf == LV_COLOR_FORMAT_RGB565 && d->header.w == w && d->header.h == h)
			return o;
	}
	for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
		lv_obj_t *r = dimensions(lv_obj_get_child(o, i), w, h, background);
		if (r)
			return r;
	}
	return NULL;
}
static bool gray_image(lv_obj_t *o, bool background) {
	if (!o)
		return false;
	const lv_image_dsc_t *d = background ? lv_obj_get_style_bg_image_src(o, 0) : lv_image_get_src(o);
	if (!d)
		return false;
	const uint16_t *p = (void *)d->data;
	for (unsigned i = 0; i < d->header.w * d->header.h; i++)
		if ((p[i] >> 11) != (p[i] & 31))
			return false;
	return true;
}
static void sample(const char *name) {
	sx_artview_stats_t s = sx_artview_stats();
	printf("CAP_STAGE %s bytes=%zu peak=%zu conversions=%u failures=%u\n", name, s.bytes, s.peak_bytes, s.conversions, s.failures);
}
static void cap_run(lv_timer_t *timer) {
	(void)timer;
	int height = lv_display_get_vertical_resolution(lv_display_get_default());
	if (cap_phase == -2) {
		gui_init_rest();
		pointer = lv_indev_get_next(NULL);
		while (pointer && lv_indev_get_type(pointer) != LV_INDEV_TYPE_POINTER)
			pointer = lv_indev_get_next(pointer);
		assert(pointer);
		lv_indev_set_read_cb(pointer, read_pointer);
		lv_timer_pause(lv_indev_get_read_timer(pointer));
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		theme_set_palette(0);
		theme_set_monochrome_covers(true);
		cap_phase = -1;
		return;
	}
	if (cap_phase == -1) {
		if (library_scan_running())
			return;
		char path[1024];
		snprintf(path, sizeof(path), "%s/Graphite/Echoes.flac", getenv("SONIX_SD_ROOT"));
		player_play_file(path);
		cap_phase = 0;
		return;
	}
	switch (cap_phase++) {
	case 0:
		if (!player_cover_image()) {
			cap_phase = 0;
			return;
		}
		device_state_toggle_play_pause();
		player_layout_set(PLAYER_LAYOUT_STUDIO);
		break;
	case 1:
		if (!player_cover_image() || !player_backdrop_image()) {
			cap_phase = 1;
			return;
		}
		grid = lv_obj_create(NULL);
		lv_screen_load(grid);
		lv_obj_update_layout(grid);
		for (int i = 0; i < 13; i++) {
			for (unsigned j = 0; j < 56 * 56; j++)
				thumb_pixels[i][j] = (uint16_t)(0xf800 + i * 23 + j % 19);
			thumb_source[i] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 56, .h = 56, .stride = 112}, .data_size = 56 * 56 * 2, .data = (void *)thumb_pixels[i]};
			thumbs[i] = lv_image_create(grid);
			lv_obj_set_pos(thumbs[i], 20 + (i % 4) * 100, 120 + (i / 4) * 90);
			assert(sx_artview_bind(thumbs[i], &thumb_source[i], cover_new_display_generation()));
		}
		switch_screen(player_screen);
		break;
	case 2:
		assert(gray_image(bound(player_screen, player_cover_image()), false));
		assert(gray_image(bound(player_screen, player_backdrop_image()), false));
		normal_bytes = sx_artview_stats().bytes;
		sample("studio-normal");
		printf("CAP_DIMS studio_cover=%ux%u/%u studio_backdrop=%ux%u/%u\n", player_cover_image()->header.w, player_cover_image()->header.h, player_cover_image()->data_size, player_backdrop_image()->header.w, player_backdrop_image()->header.h, player_backdrop_image()->data_size);
		screensaver_set_enabled(true);
		assert(screensaver_has_images());
		screensaver_set_source(SCREENSAVER_SOURCE_IMAGES);
		screensaver_show();
		break;
	case 3: {
		screensaver_prepare();
		saver = NULL;
		for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
			lv_obj_t *child = lv_obj_get_child(lv_layer_top(), i);
			if (child != player_screen && lv_obj_is_visible(child) && dimensions(child, 480, height, false)) {
				saver = child;
				break;
			}
		}
		if (!saver) {
			cap_phase = 3;
			return;
		}
		assert(gray_image(dimensions(saver, 480, height, false), false));
		assert(gray_image(dimensions(saver, 480, 250, true), true));
		saver_bytes = sx_artview_stats().bytes;
		sample("folder-saver-opaque");
		printf("CAP_DIMS folder_photo=480x%d/%u folder_strip=480x250/%u thumbnails=13x56x56/%u\n", height, 480 * height * 2, 480 * 250 * 2, 13 * 56 * 56 * 2);
		capture("folder-saver-opaque");
		input(240, height - 90, true);
		input(240, height - 110, true);
		input(240, height - 140, true);
		input(240, height - 170, true);
		overlap_bytes = sx_artview_stats().bytes;
		overlap_failures = sx_artview_stats().failures;
		sample("held-folder-over-studio-13thumbs");
		if (old_cap) {
			assert(overlap_failures > 0);
			puts("CAP_NEGATIVE old1536KiB normalstate exceeds cap");
		} else {
			assert(!overlap_failures);
			assert(gray_image(dimensions(saver, 480, height, false), false));
			assert(gray_image(dimensions(saver, 480, 250, true), true));
			assert(gray_image(bound(player_screen, player_cover_image()), false));
			assert(gray_image(bound(player_screen, player_backdrop_image()), false));
			for (int i = 0; i < 13; i++)
				assert(gray_image(thumbs[i], false));
		}
		capture("held-folder-over-studio");
		input(240, height - 170, false);
		break;
	}
	case 4:
		assert(screensaver_is_visible());
		sample("folder-saver-snapback");
		assert(sx_artview_stats().bytes <= saver_bytes + 4096);
		screensaver_hide();
		sample("folder-saver-dismiss-start");
		break;
	case 5:
		assert(!screensaver_is_visible());
		sample("studio-restored");
		assert(gray_image(bound(player_screen, player_cover_image()), false));
		assert(gray_image(bound(player_screen, player_backdrop_image()), false));
		assert(sx_artview_stats().bytes <= normal_bytes + 4096);
		player_sheet_close(false);
		sample("13thumbs-restored");
		for (int i = 0; i < 13; i++)
			assert(gray_image(thumbs[i], false));
		assert(sx_artview_stats().bytes < 200000);
		printf("CAP_RESULT expected_old=%d overlap=%zu failures_at_overlap=%u cap=%u\n", old_cap, overlap_bytes, overlap_failures, old_cap ? 1536 * 1024 : SX_ARTVIEW_BUDGET);
		puts("UI_TEST_PASS real different-folder photo/strip over Studio photo/backdrop +13thumbs, held drag/snapback/dismiss/release");
		fflush(stdout);
		logging_flush();
		exit(0);
	}
}
void graphite_ui_test_init(void) {
	signal(SIGABRT, SIG_DFL);
	out_dir = getenv("SONIX_UI_TEST_DIR");
	assert(out_dir);
	old_cap = getenv("SONIX_CAP_EXPECT_OLD") != NULL;
	lv_timer_create(cap_run, 500, NULL);
}
