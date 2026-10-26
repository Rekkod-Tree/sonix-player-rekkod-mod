// Build with make host UI_TEST=1. Run only with disposable config and media roots.
// Uses LVGL's own input and rendering API; no desktop automation is required.
#include "src/system/core/logging.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/gui/bluetooth/btaudio.h"
#include "src/gui/bluetooth/btsettings.h"
#include "src/gui/ebook/ebookpage.h"
#include "src/gui/ebook/ebookreader.h"
#include "src/gui/ebook/ebooksettings.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/gearboy/gearboypage.h"
#include "src/gui/gearboy/gearboysettings.h"
#include "src/gui/library/audiobooks.h"
#include "src/gui/library/browser.h"
#include "src/gui/library/filespage.h"
#include "src/gui/library/medialist.h"
#include "src/gui/library/music.h"
#include "src/gui/library/playlistpage.h"
#include "src/gui/library/search.h"
#include "src/gui/library/searchsource.h"
#include "src/gui/nowplaying/btline.h"
#include "src/gui/nowplaying/coverflow.h"
#include "src/gui/nowplaying/historypage.h"
#include "src/gui/nowplaying/npicons.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/nowplaying/trackmenu.h"
#include "src/gui/settings/appearance.h"
#include "src/gui/settings/ccsettings.h"
#include "src/gui/settings/devoptions.h"
#include "src/gui/settings/hometiles.h"
#include "src/gui/settings/language.h"
#include "src/gui/settings/musicsettings.h"
#include "src/gui/settings/powersettings.h"
#include "src/gui/settings/remap.h"
#include "src/gui/settings/settings.h"
#include "src/gui/settings/systempage.h"
#include "src/gui/shell/bookresume.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/main_menu.h"
#include "src/gui/shell/morepage.h"
#include "src/gui/shell/playbackplace.h"
#include "src/gui/shell/quickpanel.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/sx_mini.h"
#include "src/gui/shell/sx_palette.h"
#include "src/gui/shell/sx_row.h"
#include "src/gui/shell/sx_sheet.h"
#include "src/gui/shell/sx_styles.h"
#include "src/gui/shell/sx_toast.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/topbar.h"
#include "src/gui/shell/volume_overlay.h"
#include "src/gui/shell/welcome.h"
#include "src/gui/streaming/qobuzpage.h"
#include "src/gui/streaming/streamerror.h"
#include "src/gui/streaming/streaming.h"
#include "src/gui/streaming/subsonicpage.h"
#include "src/gui/streaming/svcui.h"
#include "src/gui/streaming/tidalpage.h"
#include "src/gui/streaming/yandexpage.h"
#include "src/gui/streaming/zvukpage.h"
#include "src/gui/wireless/wifisettings.h"
#include "src/gui/wireless/wifitransfer.h"
#include "src/gui/wireless/wireless.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/decode/decode.h"
#include "src/system/device/power.h"
#include "src/system/image/png_write.h"
#include "src/system/library/audiobookdb.h"
#include "src/system/playback/audiobook.h"
#include "src/system/playback/device_state.h"
#include "src/system/playback/playhistory.h"
#include "src/system/playback/playlist.h"
#include "src/system/streaming/provider.h"
#include "src/system/streaming/yandex.h"
#include "src/system/streaming/yandexdl.h"
#include "src/system/streaming/zvuk.h"
#include "src/system/streaming/zvukcache.h"
#include "src/system/streaming/zvukdl.h"

static const char *out_dir;
static int phase;
static int taps, likes, nexts, ends;
static int undos;
static lv_area_t undo_area;
static lv_point_t point;
static bool pressed;
static lv_indev_t *pointer;
static lv_obj_t *rows_screen;
static lv_obj_t *track_row;
static lv_obj_t *queue_row;
static lv_obj_t *audit_eq, *audit_mseb, *audit_filter, *audit_fade, *audit_playback, *audit_volume;

static const struct {
	const char *name;
	lv_obj_t **screen;
} page_audit[] = {
	{"ebooks", &ebookpage_screen},	{"reading-settings", &ebooksettings_screen}, {"game-library", &gearboypage_screen}, {"game-settings", &gearboysettings_screen}, {"file-manager", &filespage_screen}, {"bluetooth", &btsettings_screen}, {"bluetooth-audio", &btaudio_screen}, {"equalizer", &audit_eq}, {"mseb", &audit_mseb},	  {"dac-filter", &audit_filter}, {"fades", &audit_fade},		 {"playback", &audit_playback}, {"volume-limit", &audit_volume}, {"home", &main_menu_screen}, {"music", &music_screen}, {"settings", &settings_screen}, {"appearance", &appearance_screen}, {"music-settings", &musicsettings_screen}, {"power", &powersettings_screen}, {"system", &systempage_screen}, {"languages", &language_screen}, {"buttons", &remap_screen}, {"control-center-settings", &ccsettings_screen}, {"developer", &devoptions_screen}, {"networks", &wireless_screen},
	{"wifi", &wifisettings_screen}, {"wifi-transfer", &wifitransfer_screen},	 {"audiobooks", &audiobooks_screen},	{"playlists", &playlistpage_screen},		{"folders", &browser_screen},		 {"more", &morepage_screen},		{"streaming", &streaming_screen},	  {"qobuz", &qobuz_screen}, {"tidal", &tidal_screen}, {"yandex", &yandex_screen},	 {"subsonic", &subsonic_screen},
};

static void tap(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	taps++;
}
static void like(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	likes++;
}
static void next(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	nexts++;
}
static void end(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	ends++;
}
static void undo(void *user) {
	(void)user;
	undos++;
}
static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
	(void)indev;
	data->point = point;
	data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void input(int x, int y, bool down) {
	point = (lv_point_t){x, y};
	pressed = down;
	lv_indev_read(pointer);
}
static void swipe(int y, bool left) {
	int start = left ? 340 : 140;
	int delta = left ? -1 : 1;
	input(start, y, true);
	input(start + delta * 24, y, true);
	input(start + delta * 130, y, true);
	input(start + delta * 130, y, false);
}
static lv_obj_t *row(int y, const char *name, sx_row_handlers_t *handlers) {
	lv_obj_t *r = sx_row_create(rows_screen, handlers, false);
	lv_obj_set_pos(r, 16, y);
	lv_obj_set_width(r, 448);
	sx_row_set_title(r, name);
	sx_row_set_subtitle(r, "Pink Floyd", NULL, "4:32");
	sx_row_set_thumb(r, NULL, sx_placeholder_name(name));
	return r;
}
static void build_rows(void) {
	rows_screen = lv_obj_create(NULL);
	lv_obj_add_style(rows_screen, &sx_style_page, 0);
	sx_row_handlers_t h = {.on_tap = tap, .on_star = like, .on_swipe_next = next, .on_swipe_end = end};
	track_row = row(140, "The Great Gig in the Sky", &h);
	sx_row_set_trail(track_row, SX_ROW_TRAIL_STAR);
	lv_obj_t *artist = row(230, "Pink Floyd", &h);
	sx_row_set_letter(artist, "P", sx_placeholder_name("Pink Floyd"), true);
	sx_row_set_trail(artist, SX_ROW_TRAIL_CHEVRON);
	lv_obj_t *year = row(320, "1973", &h);
	sx_row_set_letter(year, "73", sx_hex(SX_CATEGORY_BG), false);
	sx_row_set_trail(year, SX_ROW_TRAIL_CHEVRON);
	h.end_is_remove = true;
	queue_row = row(410, "Eclipse", &h);
	sx_row_set_trail(queue_row, SX_ROW_TRAIL_GRIP);
	pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_pointer);
}
static void capture(const char *name) {
	lv_refr_now(NULL);
	int w, h;
	uint16_t *pixels = display_capture_frame(&w, &h);
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.png", out_dir, name);
	assert(pixels && png_write_rgb565(path, pixels, w, h));
	printf("UI_CAPTURE %s %dx%d\n", name, w, h);
}
static lv_obj_t *find_label(lv_obj_t *root, const char *text) {
	if (lv_obj_check_type(root, &lv_label_class) && strncmp(lv_label_get_text(root), text, strlen(text)) == 0)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = find_label(lv_obj_get_child(root, (int)i), text);
		if (found)
			return found;
	}
	return NULL;
}
static lv_obj_t *find_field(lv_obj_t *root) {
	if (lv_obj_check_type(root, &lv_textarea_class))
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *field = find_field(lv_obj_get_child(root, (int)i));
		if (field)
			return field;
	}
	return NULL;
}

static lv_obj_t *find_image(lv_obj_t *root, const lv_image_dsc_t *source) {
	if (lv_obj_check_type(root, &lv_image_class) && lv_image_get_src(root) == source)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *image = find_image(lv_obj_get_child(root, (int)i), source);
		if (image)
			return image;
	}
	return NULL;
}

static bool has_service_thumbnail(lv_obj_t *root) {
	if (lv_obj_check_type(root, &lv_image_class) && lv_obj_is_visible(root)) {
		const void *source = lv_image_get_src(root);
		if (source && lv_image_src_get_type(source) == LV_IMAGE_SRC_VARIABLE) {
			const lv_image_dsc_t *image = source;
			if (image->header.w == 56 && image->header.h == 56 && image->header.cf == LV_COLOR_FORMAT_RGB565)
				return true;
		}
	}
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		if (has_service_thumbnail(lv_obj_get_child(root, (int)i)))
			return true;
	}
	return false;
}

static void open_fixture_album(const char *path, const char *title) {
	char value[768];
	assert(library_track_album_value(path, value, sizeof(value)));
	medialist_open(title, LIBRARY_LIST_TRACKS, LIBRARY_FILTER_ALBUM, value);
}

static void mock_mode(const char *mode) {
	assert(strcmp(config_get_copy("zvuk", "base_url", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "http://127.0.0.1:18766") == 0);
	const char *path = getenv("SONIX_UI_TEST_MOCK_MODE");
	assert(path);
	FILE *file = fopen(path, "w");
	assert(file);
	fprintf(file, "%s\n", mode);
	assert(fclose(file) == 0);
}

static void mock_sign_in(bool subscribed) {
	char message[256];
	mock_mode("ok");
	assert(zvuk_sign_in(subscribed ? "a1b2c3d4e5f60718293a4b5c6d7e8f90" : "f1f2f3f4f5f6f7f8f9fafbfcfdfeff00", message, sizeof(message)) == CARDIMPORT_OK);
	assert(zvuk_logged_in());
	switch_screen(main_menu_screen);
}

static bool visible(lv_obj_t *obj) {
	if (!obj)
		return false;
	for (; obj; obj = lv_obj_get_parent(obj))
		if (lv_obj_is_hidden(obj))
			return false;
	return true;
}

static void error_actions(const char *primary) {
	const char *keys[] = {primary, "stream_saved_tracks"};
	for (int i = 0; i < 2; i++) {
		lv_obj_t *label = find_label(lv_screen_active(), tr(keys[i]));
		assert(visible(label));
		lv_area_t box;
		lv_obj_get_coords(lv_obj_get_parent(label), &box);
		assert(box.y2 < lv_display_get_vertical_resolution(lv_display_get_default()) - SX_MINI_SLOT_H);
		assert(box.y2 - box.y1 + 1 >= 88);
	}
	assert(!visible(find_label(lv_screen_active(), tr("zvuk_save_album"))));
}

static void no_action(void) {}

static bool spoken_wait_label(const char *text) {
	static int waits;
	if (visible(find_label(lv_screen_active(), text))) {
		waits = 0;
		return true;
	}
	if (waits == 28) {
		printf("UI_SPOKEN_MISSING_LABEL %s\n", text);
		capture("spoken-unexpected-state");
	}
	assert(++waits < 30);
	return false;
}

static lv_obj_t *spoken_exact_label(lv_obj_t *root, const char *text) {
	if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = spoken_exact_label(lv_obj_get_child(root, (int)i), text);
		if (found)
			return found;
	}
	return NULL;
}

static int spoken_kind_labels(lv_obj_t *root, const char *text) {
	int count = lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0 && visible(root);
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
		count += spoken_kind_labels(lv_obj_get_child(root, (int)i), text);
	return count;
}

static bool spoken_first_part(const char *path, const char *title, void *user) {
	(void)title;
	char *first = user;
	if (!first[0])
		snprintf(first, 512, "%s", path);
	return true;
}

static bool spoken_check_local_book(const char *book) {
	if (audiobookdb_scan_running() || !audiobookdb_book_info(book, NULL, 0, NULL, 0, NULL))
		return false;
	char first[512] = "", resumed[512];
	assert(audiobookdb_parts_for_each(book, spoken_first_part, first) == 13);
	assert(strncmp(first, book, strlen(book)) == 0 && strstr(first, "/0001-"));
	decoder_t *decoder = decoder_open(first, decode_detect_format(first));
	assert(decoder && decoder_channels(decoder) > 0 && decoder_channels(decoder) <= 2);
	int32_t pcm[256 * 2];
	assert(decoder_read_pcm_frames_s32(decoder, 256, pcm) > 0);
	decoder_close(decoder);
	audiobooks_play_at(book, first, 1.25);
	const char *loaded;
	const song_metadata_t *metadata = device_state_loaded_metadata(&loaded);
	assert(loaded && strcmp(loaded, first) == 0 && metadata->title[0]);
	double position = 0;
	assert(audiobookdb_get_position(book, resumed, sizeof(resumed), &position));
	assert(strcmp(resumed, first) == 0 && position == 1.25);
	device_state_stop();
	printf("UI_SPOKEN_LOCAL_PLAY_PASS native index, 13 ordered parts, decoded MP3, player path and resume\n");
	return true;
}

static void spoken_search(lv_obj_t *home, const char *text, const char *mode_key) {
	switch_screen(home);
	lv_obj_t *search = home == yandex_screen ? find_label(home, tr("yandex_search")) : find_image(home, &icon_search);
	assert(search && visible(search));
	lv_obj_scroll_to_view(lv_obj_get_parent(search), LV_ANIM_OFF);
	lv_obj_send_event(lv_obj_get_parent(search), LV_EVENT_CLICKED, NULL);
	lv_obj_t *field = find_field(lv_screen_active());
	assert(field);
	if (mode_key) {
		lv_obj_t *mode = find_label(lv_screen_active(), tr(mode_key));
		assert(mode && visible(mode));
		lv_obj_send_event(lv_obj_get_parent(mode), LV_EVENT_CLICKED, NULL);
	}
	lv_textarea_set_text(field, text);
	lv_obj_t *accept = find_label(lv_screen_active(), tr("ok"));
	assert(accept && visible(accept));
	if (mode_key) {
		// Yandex puts its category pills on the input screen; keep them clear
		// of the keyboard even with Large text on the shorter panel.
		assert(fonts_set_large_text(true));
		lv_obj_update_layout(lv_screen_active());
		lv_obj_t *tray = accept;
		while (lv_obj_get_parent(tray) != lv_screen_active())
			tray = lv_obj_get_parent(tray);
		lv_obj_t *pill = find_label(lv_screen_active(), tr(mode_key));
		lv_area_t keyboard, choice;
		lv_obj_get_coords(tray, &keyboard);
		lv_obj_get_coords(lv_obj_get_parent(pill), &choice);
		assert(choice.y2 < keyboard.y1);
		capture("yandex-spoken-input-large-layout");
		assert(fonts_set_large_text(false));
		field = find_field(lv_screen_active());
		assert(field && strcmp(lv_textarea_get_placeholder_text(field), tr("spoken_book_search_hint")) == 0);
		accept = find_label(lv_screen_active(), tr("ok"));
		assert(accept && visible(accept));
	}
	lv_obj_send_event(lv_obj_get_parent(accept), LV_EVENT_CLICKED, NULL);
}

static void spoken_states(void) {
	static int hold_x, hold_y, download_wait;
	static lv_obj_t *podcast_list;
	char count[100];
	switch (phase) {
	case 0: {
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		theme_set_dark(true);
		assert(strcmp(config_get_copy("zvuk", "base_url", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "http://127.0.0.1:18769") == 0);
		char message[256];
		assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		switch_screen(zvuk_screen);
		phase++;
		break;
	}
	case 1:
		spoken_search(zvuk_screen, "fixture", NULL);
		phase = 101;
		break;
	case 101: {
		if (!spoken_wait_label(tr("spoken_audiobook")))
			return;
		assert(find_field(lv_screen_active()) == NULL); // Filters and keyboard use separate screens.
		assert(fonts_set_large_text(true));
		lv_obj_t *pill = find_label(lv_screen_active(), tr("spoken_audiobook"));
		lv_obj_scroll_to_view(lv_obj_get_parent(pill), LV_ANIM_OFF);
		lv_obj_update_layout(lv_screen_active());
		lv_area_t box;
		lv_obj_get_coords(lv_obj_get_parent(pill), &box);
		assert(box.x1 >= 0 && box.x2 < 480 && box.y2 < lv_display_get_vertical_resolution(lv_display_get_default()) / 2);
		capture("spoken-search-pills-large");
		assert(fonts_set_large_text(false));
		lv_obj_send_event(lv_obj_get_parent(pill), LV_EVENT_CLICKED, NULL);
		phase = 102;
		break;
	}
	case 102: {
		if (!spoken_wait_label("Fixture Complete"))
			return;
		assert(spoken_kind_labels(lv_screen_active(), tr("spoken_audiobook")) >= 2);
		capture("spoken-book-search-result");
		lv_obj_t *title = find_label(lv_screen_active(), "Fixture Complete");
		lv_area_t box;
		lv_obj_get_coords(title, &box);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, true);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, false);
		phase = 2;
		break;
	}
	case 2: {
		if (!spoken_wait_label("Chapter 01"))
			return;
		capture("spoken-book-loaded");
		assert(visible(find_label(lv_screen_active(), "Fixture Complete")));
		lv_obj_t *kind = find_label(lv_screen_active(), tr("spoken_audiobook"));
		assert(kind && visible(kind));
		snprintf(count, sizeof(count), tr("spoken_chapter_count"), 13);
		assert(strstr(lv_label_get_text(kind), count));
		lv_obj_t *save = find_label(lv_screen_active(), tr("spoken_download_book"));
		assert(save && visible(save));
		lv_area_t area;
		lv_obj_get_coords(lv_obj_get_parent(save), &area);
		assert(area.y1 >= SX_STATUSBAR_H && area.y2 < lv_display_get_vertical_resolution(lv_display_get_default()) - SX_MINI_SLOT_H);
		assert(!visible(find_label(lv_screen_active(), tr("zvuk_save_album"))));
		capture("spoken-book-13-parts");
		lv_obj_send_event(lv_obj_get_parent(save), LV_EVENT_CLICKED, NULL);
		phase++;
		break;
	}
	case 3: {
		char path[512];
		if (!zvukdl_local_book("8101", path, sizeof(path))) {
			assert(++download_wait < 60);
			return;
		}
		if (!spoken_check_local_book(path)) {
			assert(++download_wait < 60);
			return;
		}
		assert(strstr(path, "/zvuk-8101"));
		printf("UI_SPOKEN_BOOK_SAVED complete 13 parts via real download button\n");
		capture("spoken-book-saved");
		theme_set_dark(false);
		spoken_search(zvuk_screen, "fixture", NULL);
		phase = 103;
		break;
	}
	case 103: {
		if (!spoken_wait_label(tr("spoken_podcast")))
			return;
		lv_obj_t *pill = find_label(lv_screen_active(), tr("spoken_podcast"));
		lv_obj_scroll_to_view(lv_obj_get_parent(pill), LV_ANIM_OFF);
		lv_obj_send_event(lv_obj_get_parent(pill), LV_EVENT_CLICKED, NULL);
		phase = 104;
		break;
	}
	case 104: {
		if (!spoken_wait_label("Fixture Road"))
			return;
		assert(spoken_kind_labels(lv_screen_active(), tr("spoken_podcast")) >= 2);
		capture("spoken-podcast-search-result");
		lv_obj_t *title = find_label(lv_screen_active(), "Fixture Road");
		lv_area_t box;
		lv_obj_get_coords(title, &box);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, true);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, false);
		phase = 4;
		break;
	}
	case 4: {
		if (!spoken_wait_label("Episode 338"))
			return;
		podcast_list = lv_screen_active();
		lv_obj_t *kind = find_label(podcast_list, tr("spoken_podcast"));
		assert(kind && visible(kind));
		snprintf(count, sizeof(count), tr("spoken_episode_count"), 338);
		assert(strstr(lv_label_get_text(kind), count));
		assert(visible(find_label(podcast_list, "2026-10-17")));
		assert(!visible(find_label(podcast_list, tr("spoken_download_book"))));
		assert(!visible(find_label(podcast_list, tr("zvuk_save_album"))));
		lv_obj_t *newest = find_label(podcast_list, "Episode 338");
		lv_obj_t *previous = find_label(podcast_list, "Episode 337");
		assert(newest && previous);
		lv_area_t a, b;
		lv_obj_get_coords(newest, &a);
		lv_obj_get_coords(previous, &b);
		assert(a.y1 < b.y1);
		capture("spoken-podcast-338-newest");
		hold_x = a.x1 + 20;
		hold_y = (a.y1 + a.y2) / 2;
		input(hold_x, hold_y, true);
		phase++;
		break;
	}
	case 5:
		input(hold_x, hold_y, false);
		assert(sx_sheet_is_open());
		assert(visible(find_label(lv_layer_top(), tr("spoken_download_episode"))));
		assert(!visible(find_label(lv_layer_top(), tr("spoken_download_book"))));
		assert(!visible(find_label(lv_layer_top(), tr("zvuk_save_album"))));
		capture("spoken-podcast-episode-menu");
		sx_sheet_close();
		phase++;
		break;
	case 6: {
		lv_obj_t *label = find_label(podcast_list, "Episode 338");
		lv_obj_t *scroll = lv_obj_get_parent(label);
		while (scroll && lv_obj_get_scroll_bottom(scroll) < 1000)
			scroll = lv_obj_get_parent(scroll);
		assert(scroll);
		lv_obj_scroll_to_y(scroll, lv_obj_get_scroll_y(scroll) + lv_obj_get_scroll_bottom(scroll), LV_ANIM_OFF);
		phase++;
		break;
	}
	case 7:
		if (!spoken_wait_label("Episode 001"))
			return;
		capture("spoken-podcast-lazy-last-page");
		assert(strcmp(config_get_copy("yandex", "base_url", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "http://127.0.0.1:18770") == 0);
		assert(yandex_configured() && yandex_logged_in() && yandex_screen);
		theme_set_dark(true);
		spoken_search(yandex_screen, "book", "audiobooks");
		phase++;
		break;
	case 8: {
		if (!spoken_wait_label("Симона де"))
			return;
		assert(visible(find_label(lv_screen_active(), tr("spoken_audiobook"))));
		capture("yandex-spoken-book-search");
		lv_obj_t *title = find_label(lv_screen_active(), "Симона де");
		lv_area_t box;
		lv_obj_get_coords(title, &box);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, true);
		input(box.x1 + 20, (box.y1 + box.y2) / 2, false);
		phase++;
		break;
	}
	case 9: {
		snprintf(count, sizeof(count), tr("spoken_chapter_count"), 13);
		if (!spoken_wait_label(count))
			return;
		assert(visible(find_label(lv_screen_active(), tr("spoken_download_book"))));
		assert(!visible(find_label(lv_screen_active(), tr("yandex_save_album"))));
		capture("yandex-spoken-book-13-parts");
		// The actual chapter title is long and LVGL truncates its displayed
		// buffer; the first numbered row remains an unambiguous hold target.
		lv_obj_t *first = spoken_exact_label(lv_screen_active(), "1");
		assert(first);
		lv_area_t box;
		lv_obj_get_coords(first, &box);
		hold_x = box.x1 + 20;
		hold_y = (box.y1 + box.y2) / 2;
		input(hold_x, hold_y, true);
		phase++;
		break;
	}
	case 10:
		input(hold_x, hold_y, false);
		assert(sx_sheet_is_open());
		assert(visible(find_label(lv_layer_top(), tr("spoken_download_book"))));
		assert(!visible(spoken_exact_label(lv_layer_top(), tr("yandex_save"))));
		capture("yandex-spoken-chapter-menu-whole-book");
		sx_sheet_close();
		phase++;
		break;
	case 11: {
		lv_obj_t *save = find_label(lv_screen_active(), tr("spoken_download_book"));
		assert(save && visible(save));
		lv_obj_send_event(lv_obj_get_parent(save), LV_EVENT_CLICKED, NULL);
		download_wait = 0;
		phase++;
		break;
	}
	case 12: {
		char path[512];
		if (!yandexdl_audiobook_path("38681851", path, sizeof(path))) {
			assert(++download_wait < 60);
			return;
		}
		if (!spoken_check_local_book(path)) {
			assert(++download_wait < 60);
			return;
		}
		assert(strstr(path, "/yandex-38681851"));
		printf("UI_SPOKEN_YANDEX_BOOK_SAVED complete 13 parts via real download button\n");
		capture("yandex-spoken-book-saved");
		theme_set_dark(false);
		spoken_search(yandex_screen, "https://music.yandex.ru/album/11002256", NULL);
		phase++;
		break;
	}
	case 13: {
		if (!spoken_wait_label("206: Последний кадр"))
			return;
		podcast_list = lv_screen_active();
		snprintf(count, sizeof(count), tr("spoken_episode_count"), 299);
		assert(visible(find_label(podcast_list, count)));
		assert(visible(find_label(podcast_list, "2026-10-08")));
		assert(!visible(find_label(podcast_list, tr("spoken_download_book"))));
		assert(!visible(find_label(podcast_list, tr("yandex_save_album"))));
		capture("yandex-spoken-podcast-299-newest");
		lv_obj_t *first = find_label(podcast_list, "206: Последний кадр");
		lv_area_t box;
		lv_obj_get_coords(first, &box);
		hold_x = box.x1 + 20;
		hold_y = (box.y1 + box.y2) / 2;
		input(hold_x, hold_y, true);
		phase++;
		break;
	}
	case 14:
		input(hold_x, hold_y, false);
		assert(sx_sheet_is_open());
		assert(visible(find_label(lv_layer_top(), tr("spoken_download_episode"))));
		assert(!visible(find_label(lv_layer_top(), tr("spoken_download_book"))));
		assert(!visible(spoken_exact_label(lv_layer_top(), tr("yandex_save"))));
		capture("yandex-spoken-episode-menu");
		sx_sheet_close();
		phase++;
		break;
	case 15: {
		lv_obj_t *label = find_label(podcast_list, "206: Последний кадр");
		lv_obj_t *scroll = lv_obj_get_parent(label);
		while (scroll && lv_obj_get_scroll_bottom(scroll) < 1000)
			scroll = lv_obj_get_parent(scroll);
		assert(scroll);
		lv_obj_scroll_to_y(scroll, lv_obj_get_scroll_y(scroll) + lv_obj_get_scroll_bottom(scroll), LV_ANIM_OFF);
		phase++;
		break;
	}
	case 16:
		if (!spoken_wait_label("01: Миф"))
			return;
		capture("yandex-spoken-podcast-last-page");
		printf("UI_TEST_PASS spoken: both providers, real book downloads, 338/299 episodes, dates and destination menus\n");
		logging_flush();
		exit(0);
	}
}

static lv_obj_t *long_error;
static lv_obj_t *expired_list;

static void service_states(void) {
	switch (phase++) {
	case 0:
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		theme_set_dark(true);
		sx_mini_set_track("Time", "/fixture/Time.flac", "Pink Floyd", false);
		switch_screen(zvuk_screen);
		break;
	case 1:
		capture("service-signed-out");
		mock_sign_in(true);
		break;
	case 2:
		switch_screen(zvuk_screen);
		break;
	case 3:
		capture("service-subscribed");
		break;
	case 4:
		mock_mode("401");
		assert(zvukpage_open_album("701", "Группа крови"));
		break;
	case 5:
		break;
	case 6:
		capture("service-expired");
		expired_list = lv_screen_active();
		assert(!zvuk_logged_in() && zvuk_session_expired());
		error_actions("sign_in");
		lv_obj_send_event(lv_obj_get_parent(find_label(lv_screen_active(), tr("sign_in"))), LV_EVENT_CLICKED, NULL);
		assert(lv_screen_active() == zvuk_screen);
		break;
	case 7:
		capture("service-sign-in-return");
		mock_sign_in(false);
		switch_screen(zvuk_screen);
		break;
	case 8:
		capture("service-no-subscription");
		assert(zvuk_needs_subscription());
		switch_screen(expired_list);
		break;
	case 9:
		capture("service-return-after-login");
		assert(visible(find_label(lv_screen_active(), "Закрой за мной дверь")));
		mock_mode("418");
		assert(zvukpage_open_album("702", "Blocked album"));
		break;
	case 10:
		break;
	case 11:
		capture("service-blocked");
		error_actions("stream_retry");
		break;
	case 12:
		mock_mode("500");
		assert(zvukpage_open_album("703", "Server error"));
		break;
	case 13:
		break;
	case 14:
		capture("service-server-error");
		error_actions("stream_retry");
		break;
	case 15:
		mock_mode("html");
		assert(zvukpage_open_album("701", "Invalid response"));
		break;
	case 16:
		break;
	case 17:
		capture("service-invalid-response");
		break;
	case 18:
		mock_mode("ok");
		assert(visible(find_label(lv_screen_active(), tr("stream_retry"))));
		lv_obj_send_event(lv_obj_get_parent(find_label(lv_screen_active(), tr("stream_retry"))), LV_EVENT_CLICKED, NULL);
		break;
	case 19:
		break;
	case 20:
		capture("service-recovered");
		assert(find_label(lv_screen_active(), "Закрой за мной дверь"));
		theme_set_dark(false);
		break;
	case 21:
		mock_mode("500");
		assert(zvukpage_open_album("702", "Server error"));
		break;
	case 22:
		break;
	case 23:
		capture("service-error-light");
		error_actions("stream_retry");
		break;
	case 24: {
		assert(fonts_set_large_text(true));
		lv_obj_t *screen = lv_obj_create(NULL);
		lv_obj_add_style(screen, &sx_style_page, 0);
		long_error = streamerror_create(screen);
		lv_obj_set_width(long_error, 448);
		lv_obj_align(long_error, LV_ALIGN_TOP_MID, 0, SX_BODY_TOP);
		streamerror_set_offline(long_error, no_action);
		switch_screen(screen);
		streamerror_show(long_error, STREAM_ERR_OTHER, "The service cannot complete this request at the moment. Your saved music is still available. Check your connection and try again. This long diagnostic message explains the problem without moving the action buttons below the screen. The interface must remain usable with larger text and on the shorter screen. You can return to saved tracks at any time.", no_action, NULL);
		break;
	}
	case 25:
		capture("service-long-large");
		error_actions("stream_retry");
		break;
	case 26: {
		lv_obj_t *body = lv_obj_get_child(long_error, 0);
		lv_area_t box;
		lv_obj_get_coords(body, &box);
		input(240, box.y2 - 24, true);
		input(240, box.y2 - 48, true);
		input(240, box.y1 + 24, true);
		input(240, box.y1 + 24, false);
		break;
	}
	case 27:
		capture("service-long-large-scrolled");
		assert(lv_obj_get_scroll_y(lv_obj_get_child(long_error, 0)) > 0);
		error_actions("stream_retry");
		break;
	case 28: {
		char message[256];
		mock_mode("profile307");
		assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		assert(zvuk_logged_in());
		mock_mode("profile308");
		assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		assert(zvuk_sign_in("invalid-fixture-token-0000000000", message, sizeof(message)) == CARDIMPORT_REFUSED);
		printf("UI_REDIRECT_LOGIN_PASS 307/308 profile fallback and invalid token rejection\n");
		mock_mode("ok");
		assert(fonts_set_large_text(false));
		switch_screen(zvuk_screen);
		lv_obj_t *albums = find_label(zvuk_screen, tr("zvuk_albums"));
		assert(albums);
		lv_obj_send_event(lv_obj_get_parent(albums), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 29:
		break;
	case 30:
		capture("service-album-thumbnails");
		{
			const char *setting = getenv("SONIX_UI_TEST_LIST_ART");
			bool opted_in = setting && strcmp(setting, "1") == 0;
			assert(has_service_thumbnail(lv_screen_active()) == opted_in);
			printf("UI_LIST_ART_PASS %s: page binding, local HTTP and rendered image state\n", opted_in ? "opt-in" : "default off");
		}
		printf("UI_TEST_PASS service states: expired, subscription, faults and recovery\n");
		logging_flush();
		exit(0);
	}
}
static void status_layout(lv_obj_t *bar, lv_obj_t *left, lv_obj_t *right) {
	lv_obj_update_layout(bar);
	lv_obj_t *items[24];
	int count = 0;
	lv_obj_t *groups[] = {left, right, bar};
	for (int g = 0; g < 3; g++) {
		for (uint32_t i = 0; i < lv_obj_get_child_count(groups[g]); i++) {
			lv_obj_t *item = lv_obj_get_child(groups[g], i);
			if (item == left || item == right || lv_obj_is_hidden(item))
				continue;
			items[count++] = item;
		}
	}
	for (int i = 0; i < count; i++) {
		lv_area_t a;
		lv_obj_get_coords(items[i], &a);
		assert(a.x1 >= 16 && a.x2 < 464 && a.y1 >= 0 && a.y2 < 44);
		for (int j = i + 1; j < count; j++) {
			lv_area_t b;
			lv_obj_get_coords(items[j], &b);
			assert(a.x2 + 4 <= b.x1 || b.x2 + 4 <= a.x1);
		}
	}
}

static void status_states(void) {
	if (phase++ == 0) {
		gui_init_rest();
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		switch_screen(main_menu_screen);
		sx_mini_set_track("Time", "/fixture/Time.flac", "Pink Floyd", false);
		return;
	}
	topbar_refresh_volume(40);
	lv_obj_t *vol = find_image(lv_layer_top(), &icon_volume_low);
	assert(vol);
	lv_obj_t *left = lv_obj_get_parent(vol);
	lv_obj_t *bar = lv_obj_get_parent(left);
	lv_obj_t *bt = find_image(bar, &icon_bluetooth_status);
	lv_obj_t *right = lv_obj_get_parent(bt);
	lv_obj_t *clock = lv_obj_get_child(bar, 2);
	assert(lv_obj_check_type(clock, &lv_label_class));
	lv_label_set_text(clock, "18:46");
	assert(!find_image(bar, &icon_moon_status));
	assert(!find_image(bar, &icon_play_status));
	assert(!find_image(bar, &icon_pause_status));
	lv_obj_t *battery = find_label(right, "--%");
	assert(battery);
	lv_label_set_text(battery, "100%");
	lv_obj_t *shell = find_image(bar, &icon_battery);
	assert(shell && lv_obj_get_width(shell) == 24);
	lv_obj_t *fill = lv_obj_get_child(lv_obj_get_parent(shell), 0);
	lv_obj_set_hidden(fill, false);
	lv_obj_set_width(fill, 14);
	// Simulate the busiest indicator combination without changing real radios.
	for (uint32_t i = 0; i < lv_obj_get_child_count(left); i++)
		lv_obj_set_hidden(lv_obj_get_child(left, i), false);
	for (uint32_t i = 0; i < lv_obj_get_child_count(right); i++)
		lv_obj_set_hidden(lv_obj_get_child(right, i), false);
	lv_obj_t *wifi = find_image(bar, &icon_wifi_max);
	lv_obj_set_style_image_opa(bt, LV_OPA_COVER, 0);
	lv_obj_set_style_image_opa(wifi, LV_OPA_COVER, 0);
	for (int large = 0; large < 2; large++) {
		fonts_set_large_text(large != 0);
		for (int dark = 0; dark < 2; dark++) {
			theme_set_dark(dark != 0);
			lv_color_t ink = theme()->text_secondary;
			assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), ink));
			assert(lv_color_eq(lv_obj_get_style_image_recolor(shell, 0), ink));
			assert(lv_color_eq(lv_obj_get_style_bg_color(fill, 0), ink));
			for (int pos = 0; pos < 3; pos++) {
				topbar_set_clock_position(pos);
				status_layout(bar, left, right);
				char name[80];
				snprintf(name, sizeof(name), "status-%s-%s-clock-%d", dark ? "dark" : "light", large ? "large" : "normal", pos);
				capture(name);
				lv_label_set_text(clock, "12:59 PM");
				topbar_refresh_volume(100);
				status_layout(bar, left, right);
				snprintf(name, sizeof(name), "status-%s-%s-12h-clock-%d", dark ? "dark" : "light", large ? "large" : "normal", pos);
				capture(name);
				topbar_set_control_panel(true);
				status_layout(bar, left, right);
				assert(lv_color_eq(lv_obj_get_style_bg_color(bar, 0), sx_pal()->mini_bg));
				assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), sx_pal()->mini_ink));
				assert(lv_obj_get_style_bg_opa(find_label(left, "100"), 0) == LV_OPA_COVER);
				snprintf(name, sizeof(name), "status-control-%d-%d-%d", dark, large, pos);
				capture(name);
				topbar_set_control_panel(false);
				assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), ink));
				lv_label_set_text(clock, "18:46");
				topbar_refresh_volume(40);
			}
			topbar_refresh_volume(68);
			lv_obj_t *number = find_label(left, "68");
			assert(number && !lv_color_eq(lv_obj_get_style_text_color(number, 0), ink));
			capture(dark ? (large ? "status-warning-dark-large" : "status-warning-dark") : (large ? "status-warning-light-large" : "status-warning-light"));
			topbar_refresh_volume(40);
		}
	}
	fonts_set_large_text(false);
	topbar_set_clock_position(TOPBAR_CLOCK_CENTER);
	topbar_tint_on_screen(main_menu_screen, true, lv_color_hex(0xE2C2C7), lv_color_hex(0x453037));
	assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), lv_color_hex(0x453037)));
	assert(lv_color_eq(lv_obj_get_style_bg_color(fill, 0), lv_color_hex(0x453037)));
	capture("status-album-tint");
	topbar_set_over_artwork(true, false);
	assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), lv_color_white()));
	assert(lv_color_eq(lv_obj_get_style_bg_color(fill, 0), lv_color_white()));
	capture("status-over-artwork");
	topbar_set_over_artwork(false, false);
	topbar_tint_on_screen(main_menu_screen, false, lv_color_black(), lv_color_white());
	assert(lv_color_eq(lv_obj_get_style_image_recolor(bt, 0), theme()->text_secondary));
	topbar_set_clock_position(TOPBAR_CLOCK_HIDDEN);
	assert(lv_obj_is_hidden(clock));
	lv_obj_set_style_image_opa(bt, LV_OPA_40, 0);
	capture("status-hidden-clock-idle-radio");
	printf("UI_TEST_PASS status colours, busiest layout, clock positions, large text and artwork transitions\n");
	fflush(stdout);
	exit(0);
}

static lv_obj_t *find_slider(lv_obj_t *root) {
	if (lv_obj_check_type(root, &lv_slider_class))
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = find_slider(lv_obj_get_child(root, i));
		if (found)
			return found;
	}
	return NULL;
}

static void surface_states(void) {
	int step = phase++;
	if (step == 0) {
		gui_init_rest();
		if (getenv("SONIX_UI_TEST_GRAY"))
			assert(theme_get_accent() == THEME_ACCENT_GRAY);
		switch_screen(main_menu_screen);
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		return;
	}
	bool gray_mode = getenv("SONIX_UI_TEST_GRAY") != NULL;
	int cycle = gray_mode ? 5 : 4;
	int variant = (step - 1) / cycle;
	int action = (step - 1) % cycle;
	if (variant >= 4) {
		printf("UI_TEST_PASS shared mini/control/volume colours and flat player in both themes with cover tint\n");
		logging_flush();
		exit(0);
	}
	if (gray_mode && action == 1) {
		assert(lv_screen_active() == appearance_screen);
		lv_obj_t *label = find_label(appearance_screen, tr("appearance_gray"));
		lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(label)));
		lv_area_t area;
		lv_obj_get_coords(card, &area);
		assert(area.y1 >= SX_BODY_TOP && area.y2 < lv_display_get_vertical_resolution(lv_display_get_default()) - 128);
		char palette_name[40];
		snprintf(palette_name, sizeof(palette_name), "gray-palette-%d", variant);
		capture(palette_name);
		switch_screen(main_menu_screen);
		volume_overlay_show(variant < 2 ? 40 : 68);
		return;
	}
	if (gray_mode && action > 1)
		action--;
	char name[80];
	snprintf(name, sizeof(name), "surface-%d-%d", variant, action);
	const sx_palette_t *p = sx_pal();
	if (action == 0) {
		player_sheet_close(false);
		switch_screen(main_menu_screen);
		theme_set_dark(variant % 2 == 0);
		theme_set_accent(getenv("SONIX_UI_TEST_GRAY") ? THEME_ACCENT_GRAY : variant < 2 ? THEME_ACCENT_GRAPHITE : -1);
		theme_set_dynamic_tint(variant >= 2);
		if (getenv("SONIX_UI_TEST_GRAY")) {
			assert(theme_get_accent() == THEME_ACCENT_GRAY);
			assert(config_get_int("ui", "accent", -999) == THEME_ACCENT_GRAY);
			assert(lv_color_eq(theme_accent_preset(THEME_ACCENT_GRAY), sx_hex(variant % 2 == 0 ? 0xB8B8B8 : 0x626262)));
			switch_screen(appearance_screen);
			lv_obj_t *gray = find_label(appearance_screen, tr("appearance_gray"));
			assert(visible(gray));
			lv_obj_scroll_to_view(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(gray))), LV_ANIM_OFF);
			return;
		}
		volume_overlay_show(variant < 2 ? 40 : 68);
	} else if (action == 1) {
		lv_obj_t *overlay = NULL;
		for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
			lv_obj_t *candidate = lv_obj_get_child(lv_layer_top(), i);
			if (lv_obj_get_width(candidate) == SX_VOL_OSD_W && lv_obj_get_height(candidate) == SX_VOL_OSD_H)
				overlay = candidate;
		}
		assert(overlay && visible(overlay));
		assert(lv_color_eq(lv_obj_get_style_bg_color(overlay, 0), p->mini_bg));
		assert(lv_color_eq(lv_obj_get_style_bg_color(find_slider(overlay), LV_PART_INDICATOR), p->mini_ink));
		capture(name);
		quickpanel_open();
	} else if (action == 2) {
		lv_obj_t *icon = find_image(lv_layer_top(), &icon_wifi);
		assert(icon);
		lv_obj_t *button = lv_obj_get_parent(icon);
		lv_obj_t *cell = lv_obj_get_parent(button);
		lv_obj_t *panel = lv_obj_get_parent(lv_obj_get_parent(cell));
		assert(visible(panel) && quickpanel_is_open());
		assert(lv_color_eq(lv_obj_get_style_bg_color(panel, 0), p->mini_bg));
		assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(cell, 1), 0), p->mini_ink));
		assert(lv_color_eq(lv_obj_get_style_bg_color(find_slider(panel), LV_PART_INDICATOR), p->mini_ink));
		capture(name);
		quickpanel_close();
		player_sheet_open(false);
	} else {
		assert(!quickpanel_is_open());
		assert(lv_color_eq(lv_obj_get_style_bg_color(player_screen, 0), p->bg));
		assert(!lv_obj_get_style_bg_image_src(player_screen, 0));
		lv_obj_t *title = find_label(player_screen, "Time");
		assert(title);
		lv_obj_t *menu = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(title)));
		assert(lv_color_eq(lv_obj_get_style_bg_color(menu, 0), p->bg));
		assert(lv_color_eq(lv_obj_get_style_text_color(title, 0), p->text));
		capture(name);
	}
}

#include "graphite_components.inc"
#include "graphite_feedback.inc"
#include "graphite_upstream.inc"

static void player_home_gestures(void) {
	int height = lv_display_get_vertical_resolution(lv_display_get_default());
	device_state_t before, after;
	device_state_get(&before);
	{
		player_sheet_close(false);
		switch_screen_no_history(music_screen);
		player_sheet_open(false);
		lv_obj_update_layout(player_screen);
		capture("home-gesture-player-standard");
		int bottom = height - 8;
		input(5, height - 110, true);
		input(5, height - 210, true);
		input(5, height - 210, false);
		assert(player_sheet_is_open()); // An upward drag outside the edge is not Home.
		assert(!sx_sheet_is_open());
		input(240, bottom, true);
		input(240, bottom, false);
		assert(player_sheet_is_open());
		input(240, bottom, true);
		input(240, bottom - 40, true);
		input(240, bottom - 40, false);
		assert(player_sheet_is_open());
		input(240, bottom, true);
		input(350, bottom - 20, true);
		input(350, bottom - 20, false);
		assert(player_sheet_is_open());
		input(240, bottom, true);
		input(240, bottom - 100, true);
		assert(player_sheet_is_open());
		lv_obj_send_event(player_screen, LV_EVENT_PRESS_LOST, pointer);
		input(240, bottom - 100, false);
		assert(player_sheet_is_open() && lv_screen_active() == music_screen);
		input(240, bottom, true);
		input(240, bottom - 25, true);
		input(240, bottom - 100, true);
		assert(player_sheet_is_open()); // Home commits on release, not while held.
		input(240, bottom - 100, false);
		assert(!player_sheet_is_open() && lv_screen_active() == main_menu_screen);
		capture("player-home-standard");
		// Home resets the stack, including the previous Music page.
		back_btn_cb(NULL);
		assert(lv_screen_active() == main_menu_screen);
		// Natural starts above the edge and quick/diagonal flicks must work,
		// even when the press begins on a transport button.
		for (int inset = 12; inset <= 96; inset += 28) {
			player_sheet_open(false);
			lv_obj_update_layout(player_screen);
			input(240, height - inset, true);
			input(258, height - inset - 3, true);
			input(268, height - inset - 60, false);
			assert(!player_sheet_is_open() && lv_screen_active() == main_menu_screen);
			device_state_get(&after);
			assert(strcmp(before.current_file, after.current_file) == 0 && before.volume == after.volume && before.status == after.status);
		}
		player_sheet_open(false);
		lv_obj_update_layout(player_screen);
		lv_obj_t *play = find_image(player_screen, &icon_pause);
		if (!play)
			play = find_image(player_screen, &icon_play);
		assert(play);
		lv_area_t button;
		lv_obj_get_coords(lv_obj_get_parent(play), &button);
		int x = (button.x1 + button.x2) / 2, y = (button.y1 + button.y2) / 2;
		input(x, y, true);
		input(x, y, false);
		device_state_get(&after);
		assert(after.status != before.status && player_sheet_is_open());
		input(x, y, true);
		input(x, y, false);
		device_state_get(&after);
		assert(after.status == before.status && player_sheet_is_open());
		if (y >= height - 96) {
			input(x, y, true);
			input(x, y - 30, false);
			device_state_get(&after);
			assert(after.status == before.status && player_sheet_is_open());
			input(x, y, true);
			input(x + 100, y - 20, false);
			device_state_get(&after);
			assert(after.status == before.status && player_sheet_is_open());
			input(x, y, true);
			input(x, y + 15, false);
			device_state_get(&after);
			assert(after.status == before.status && player_sheet_is_open());
		}
	}
	device_state_get(&after);
	assert(strcmp(before.current_file, after.current_file) == 0 && before.volume == after.volume && before.status == after.status);
	player_sheet_open(false);
	printf("UI_HOME_GESTURE_PASS %s: taps/short/horizontal/cancel/held/Home/quick/diagonal/wide-start, standard layout\n", theme_is_dark() ? "dark" : "light");
}

static lv_obj_t *find_visible_image(lv_obj_t *root, const lv_image_dsc_t *glyph) {
	if (lv_obj_is_hidden(root))
		return NULL;
	if (lv_obj_check_type(root, &lv_image_class) && lv_image_get_src(root) == glyph)
		return root;
	for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
		lv_obj_t *found = find_visible_image(lv_obj_get_child(root, i), glyph);
		if (found)
			return found;
	}
	return NULL;
}

static void home_layout_states(void) {
	static bool startup_clicked;
	if (!startup_clicked) {
		lv_obj_t *service = find_label(main_menu_screen, tr("zvuk"));
		assert(service);
		lv_obj_send_event(lv_obj_get_parent(service), LV_EVENT_CLICKED, NULL);
		startup_clicked = true;
		return;
	}
	static int step;
	static char book[512];
	grid_entry_t entry;
	switch (step++) {
	case 0:
		assert(lv_screen_active() == zvuk_screen);
		gui_init_rest();
		pointer = lv_indev_create();
		lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		theme_set_dark(false);
		main_menu_reset();
		assert(main_menu_count() == 5 && main_menu_at(4) == HOME_SETTINGS);
		config_set("home", "tiles", "unknown,music,music,music_albums,search,files,continue_book,settings");
		main_menu_reload();
		assert(main_menu_count() == 5 && main_menu_at(1) == HOME_MUSIC_ALBUMS);
		assert(main_menu_at(4) == HOME_CONTINUE_BOOK && !main_menu_contains(HOME_SETTINGS));
		assert(!main_menu_move(HOME_SETTINGS, 0));
		config_set("home", "tiles", "");
		main_menu_reload();
		assert(main_menu_count() == 0);
		switch_screen_no_history(main_menu_screen);
		break;
	case 1: {
		lv_obj_t *more = find_label(main_menu_screen, tr("more"));
		assert(more);
		lv_obj_t *tile = lv_obj_get_parent(more), *grid = lv_obj_get_parent(tile);
		assert(lv_obj_get_child_count(grid) == 6 && lv_obj_get_child(grid, 5) == tile);
		assert(lv_obj_get_child_count(lv_obj_get_child(grid, 0)) == 0);
		switch_screen_no_history(morepage_screen);
		lv_obj_update_layout(morepage_screen);
		assert(find_label(morepage_screen, tr("settings")) && find_label(morepage_screen, tr("music")));
		assert(find_label(morepage_screen, tr("dac")) && find_label(morepage_screen, tr("file_explorer")));
		switch_screen_no_history(main_menu_screen);
		capture("home-empty-fixed-more");
		assert(main_menu_move(HOME_MUSIC_ALBUMS, 0));
		assert(main_menu_move(HOME_MUSIC_BROWSE, 1));
		assert(main_menu_move(HOME_SEARCH, 2));
		assert(main_menu_move(HOME_CONTINUE_BOOK, 3));
		assert(main_menu_move(HOME_STREAMING, 4));
		assert(main_menu_move(HOME_SEARCH, 0));
		main_menu_reload();
		assert(main_menu_at(0) == HOME_SEARCH && main_menu_at(1) == HOME_MUSIC_ALBUMS);
		sx_mini_set_track("Time", "/fixture/Time.flac", "Pink Floyd", false);
		sx_mini_set_playing(false);
		break;
	}
	case 2:
		capture("home-shortcuts-light");
		main_menu_open_settings();
		break;
	case 3:
		assert(lv_screen_active() == hometiles_screen);
		assert(find_label(hometiles_screen, tr("hometiles_add")));
		capture("home-editor-light");
		{
			lv_obj_t *name = find_label(hometiles_screen, tr("home_search"));
			assert(name);
			lv_obj_t *grip = lv_obj_get_child(lv_obj_get_parent(name), -1);
			lv_area_t box;
			lv_obj_get_coords(grip, &box);
			int x = (box.x1 + box.x2) / 2, y = (box.y1 + box.y2) / 2;
			input(x, y, true);
			input(x, y + 24, true);
			input(x, y + 100, true);
			input(x, y + 100, false);
			assert(main_menu_at(1) == HOME_SEARCH);
		}
		theme_set_dark(true);
		break;
	case 4:
		capture("home-editor-dark");
		switch_screen(main_menu_screen);
		break;
	case 5:
		capture("home-shortcuts-dark");
		assert(main_menu_get_entry(HOME_MUSIC_ALBUMS, &entry) && entry.action);
		entry.action();
		break;
	case 6:
		assert(lv_screen_active() != main_menu_screen);
		back_btn_cb(NULL);
		break;
	case 7:
		assert(lv_screen_active() == main_menu_screen);
		quickpanel_open();
		break;
	case 8:
		capture("home-settings-in-panel");
		quickpanel_close();
		assert(getenv("SONIX_UI_TEST_BOOK"));
		snprintf(book, sizeof(book), "%s", getenv("SONIX_UI_TEST_BOOK"));
		assert(strlen(book) > 127);
		char books_root[512];
		snprintf(books_root, sizeof(books_root), "%s/Audiobooks", getenv("SONIX_SD_ROOT"));
		assert(audiobookdb_scan_start(books_root));
		break;
	case 9:
		if (audiobookdb_scan_running()) {
			step--;
			break;
		}
		assert(audiobookdb_count() > 0);
		audiobooks_play_at(book, book, 17);
		break;
	case 10: {
		device_state_t state;
		device_state_get(&state);
		assert(state.status == AUDIO_STATUS_PLAYING);
		bookresume_note(&state);
		assert(config_get_copy("home", "resume_book", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE)[0]);
		assert(config_get_copy("home", "resume_book_1", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE)[0]);
		struct stat first, last;
		assert(!stat(config_path_copy((char[512]){0}, 512), &first));
		for (int i = 0; i < 100; i++)
			bookresume_note(&state);
		assert(!stat(config_path_copy((char[512]){0}, 512), &last));
		assert(first.st_mtim.tv_sec == last.st_mtim.tv_sec && first.st_mtim.tv_nsec == last.st_mtim.tv_nsec);
		device_state_toggle_play_pause();
		break;
	}
	case 11:
		bookresume_play();
		break;
	case 12: {
		device_state_t before, after;
		device_state_get(&before);
		assert(before.status == AUDIO_STATUS_PLAYING && before.progress_current_secs >= 16);
		bookresume_play();
		device_state_get(&after);
		assert(after.progress_current_secs >= before.progress_current_secs - .2);
		assert(lv_screen_active() == main_menu_screen);
		capture("home-book-resumed");
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		break;
	}
	case 13: {
		device_state_t state;
		device_state_get(&state);
		bookresume_note(&state);
		assert(!strcmp(config_get_copy("home", "resume_title", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "Home resume"));
		main_menu_hide(HOME_CONTINUE_BOOK);
		assert(config_get_copy("home", "resume_book", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE)[0]);
		bookresume_play();
		break;
	}
	case 14: {
		device_state_t state;
		device_state_get(&state);
		assert(!strcmp(state.current_file, book) && state.progress_current_secs >= 16);
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		break;
	}
	case 15: {
		char moved[600];
		snprintf(moved, sizeof(moved), "%s.moved", book);
		assert(!rename(book, moved));
		bookresume_play();
		device_state_t state;
		device_state_get(&state);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		assert(lv_screen_active() == audiobooks_screen);
		assert(!rename(moved, book));
		sx_toast_dismiss();
		audiobookdb_mark_finished(book);
		bookresume_play();
		assert(!sx_sheet_is_open());
		assert(lv_screen_active() == audiobooks_screen);
		capture("home-finished-book-choose-next");
		break;
	}
	case 16: {
		device_state_t state;
		device_state_get(&state);
		assert(!strcmp(state.current_file, getenv("SONIX_UI_TEST_TRACK")));
		assert(lv_screen_active() == audiobooks_screen);
		assert(!sx_sheet_is_open());
		switch_screen(main_menu_screen);
		capture("home-finished-book-choose-label");
		main_menu_reset();
		assert(config_get_copy("home", "resume_book", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE)[0]);
		main_menu_open_search();
		lv_obj_t *heading = find_label(lv_screen_active(), tr("search_everywhere"));
		assert(heading);
		lv_obj_send_event(heading, LV_EVENT_SHORT_CLICKED, NULL);
		assert(sx_sheet_is_open());
		lv_obj_t *scope = find_label(lv_layer_top(), tr("home_search_card"));
		assert(scope);
		capture("home-search-scope");
		lv_obj_send_event(lv_obj_get_parent(scope), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 17:
		assert(lv_screen_active() == search_screen);
		capture("home-local-search");
		main_menu_open_settings();
		break;
	case 18: {
		lv_obj_t *name = find_label(hometiles_screen, tr("settings"));
		assert(name);
		lv_obj_update_layout(hometiles_screen);
		lv_area_t area;
		lv_obj_get_coords(name, &area);
		int x = (area.x1 + area.x2) / 2;
		int y = (area.y1 + area.y2) / 2;
		input(x, y, true);
		input(x, y, false);
		break;
	}
	case 19: {
		lv_obj_t *remove = find_label(lv_layer_top(), tr("hometiles_to_more"));
		assert(remove);
		lv_obj_send_event(lv_obj_get_parent(remove), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 20: {
		assert(!main_menu_contains(HOME_SETTINGS) && main_menu_count() == 4);
		lv_obj_t *add = find_label(hometiles_screen, tr("hometiles_add"));
		assert(add);
		lv_obj_send_event(lv_obj_get_parent(add), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 21: {
		lv_obj_t *category = find_label(lv_layer_top(), tr("streaming"));
		assert(category);
		lv_obj_send_event(lv_obj_get_parent(category), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 22: {
		lv_obj_t *service = find_label(lv_layer_top(), tr("zvuk"));
		assert(service);
		lv_obj_send_event(lv_obj_get_parent(service), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 23:
		assert(main_menu_count() == 5 && main_menu_contains(HOME_ZVUK));
		capture("home-editor-added-service");
		switch_screen_no_history(main_menu_screen);
		break;
	case 24:
		capture("home-service-pinned");
		quickpanel_open();
		break;
	case 25: {
		lv_obj_t *gear = find_visible_image(lv_layer_top(), &icon_music_settings);
		assert(gear);
		lv_obj_send_event(lv_obj_get_parent(gear), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 26:
		assert(lv_screen_active() == settings_screen && !quickpanel_is_open());
		capture("home-gear-settings");
		{
			lv_obj_t *appearance = find_label(settings_screen, tr("appearance"));
			assert(appearance);
			lv_obj_send_event(lv_obj_get_parent(appearance), LV_EVENT_CLICKED, NULL);
		}
		break;
	case 27: {
		assert(lv_screen_active() == appearance_screen);
		lv_obj_t *home = find_label(appearance_screen, tr("home_settings"));
		assert(home);
		lv_obj_send_event(lv_obj_get_parent(home), LV_EVENT_CLICKED, NULL);
		break;
	}
	case 28:
		assert(lv_screen_active() == hometiles_screen);
		back_btn_cb(NULL);
		break;
	case 29:
		assert(lv_screen_active() == appearance_screen);
		back_btn_cb(NULL);
		break;
	case 30:
		assert(lv_screen_active() == settings_screen);
		back_btn_cb(NULL);
		break;
	case 31:
		assert(lv_screen_active() == main_menu_screen);
		puts("UI_TEST_PASS Home: malformed/duplicate/empty/full/persisted layout, fixed More, editor, direct album/back, book resume and finished guard");
		fflush(stdout);
		exit(0);
	}
}

#include "src/system/playback/sleeptimer.h"
// Scenario includes share helpers in declaration order.
// clang-format off
#include "graphite_quick_actions.inc"
#include "graphite_sleep_wait.inc"
#include "graphite_catalog.inc"
#include "graphite_spotify.inc"
#include "graphite_queue_export.inc"
#include "graphite_playlist_roles.inc"
#include "graphite_playlist_content.inc"
#include "graphite_import_mini.inc"
#include "graphite_caret_park.inc"
#include "graphite_search_scope.inc"
#include "graphite_release_polish.inc"
#include "graphite_pause_wait.inc"
#include "graphite_ux_context.inc"
#include "graphite_chrome_edges.inc"
#include "graphite_queue_modes.inc"
#include "graphite_gesture_navigation.inc"
// clang-format on

static void run(lv_timer_t *timer) {
	if (getenv("SONIX_UI_TEST_QUEUE_MODES")) {
		queue_mode_states(timer);
		return;
	}
	if (getenv("SONIX_UI_TEST_GESTURE_NAV")) {
		gesture_navigation_states(timer);
		return;
	}
	if (getenv("SONIX_UI_TEST_CHROME_EDGES")) {
		chrome_edges_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SEARCH_SOURCES")) {
		ux_sources_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SEARCH_NETWORK")) {
		ux_network_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_UX_CONTEXT")) {
		ux_context_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_PAUSE_WAIT")) {
		pause_wait_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_RELEASE_POLISH")) {
		release_polish_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_PLAYLIST_CONTENT")) {
		playlist_content_states(timer);
		return;
	}
	if (getenv("SONIX_UI_TEST_IMPORT_MINI")) {
		import_mini_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_CARET_PARK")) {
		caret_park_states(timer);
		return;
	}
	if (getenv("SONIX_UI_TEST_SEARCH_SCOPE")) {
		search_scope_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_QUEUE_EXPORT")) {
		queue_export_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_PLAYLIST_ROLES")) {
		playlist_roles_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_FEEDBACK")) {
		feedback_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SPOTIFY")) {
		spotify_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_CATALOG")) {
		catalog_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SLEEP_WAIT")) {
		sleep_wait_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_QUICK_ACTIONS")) {
		quick_actions_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_HOME_LAYOUT")) {
		home_layout_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_PLAYER_HOME_ONLY")) {
		static int step;
		int height = lv_display_get_vertical_resolution(lv_display_get_default());
		if (step == 0) {
			gui_init_rest();
			pointer = lv_indev_create();
			lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
			lv_indev_set_read_cb(pointer, read_pointer);
			theme_set_dark(false);
			player_play_file(getenv("SONIX_UI_TEST_TRACK"));
			switch_screen_no_history(music_screen);
			player_sheet_open(false);
		} else if (step == 1 || step == 3) {
			player_home_gestures();
			input(240, height - 8, true);
			input(240, height - 35, true);
			input(240, height - 120, true);
			input(240, height - 120, false);
			assert(!player_sheet_is_open());
		} else {
			assert(lv_obj_is_hidden(player_screen) && lv_screen_active() == main_menu_screen && sx_mini_visible());
			capture(step == 2 ? "home-ready-light" : "home-ready-dark");
			if (step == 4) {
				puts("UI_TEST_PASS player Home gesture and completed animation, both themes and standard layout");
				fflush(stdout);
				exit(0);
			}
			theme_set_dark(true);
			switch_screen_no_history(music_screen);
			player_sheet_open(false);
		}
		step++;
		return;
	}
	if (getenv("SONIX_UI_TEST_UPSTREAM")) {
		upstream_run();
		return;
	}
	(void)timer;
	if (getenv("SONIX_UI_TEST_SPOKEN")) {
		spoken_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_KEYBOARD_FIELDS_ONLY")) {
		keyboard_field_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_IDLE_PANEL_ONLY")) {
		panel_idle_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_REFINEMENTS_ONLY")) {
		refinement_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_COMPONENTS_ONLY")) {
		component_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SAVE_ONLY")) {
		save_menu_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SURFACES_ONLY")) {
		surface_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_STATUS_ONLY")) {
		status_states();
		return;
	}
	if (getenv("SONIX_UI_TEST_SERVICE_ONLY")) {
		service_states();
		return;
	}
	switch (phase++) {
	case 0:
		gui_init_rest();
		audit_eq = musicsettings_eq_screen();
		audit_mseb = musicsettings_mseb_screen();
		audit_filter = musicsettings_filter_screen();
		audit_fade = musicsettings_fade_screen();
		audit_playback = musicsettings_playback_screen();
		audit_volume = musicsettings_volume_limit_screen();
		if (getenv("SONIX_UI_FONTS_DIR")) {
			assert(strstr(fonts_summary(), "ui-regular.ttf, ui-semibold.ttf"));
			lv_font_glyph_dsc_t glyph;
			assert(lv_font_get_glyph_dsc(&font_ui_24, &glyph, 0x0416, 0));
			assert(glyph.resolved_font == &font_ui_24);
			assert(lv_font_get_glyph_dsc(&font_ui_24_bold, &glyph, 0x0416, 0));
			assert(glyph.resolved_font == &font_ui_24_bold);
			assert(lv_font_get_glyph_dsc(&font_ui_24, &glyph, 0x4E2D, 0));
			assert(glyph.resolved_font != &font_ui_24);
			printf("UI_FONTS_PASS prototype regular/semibold Cyrillic and CJK fallback\n");
		}
		assert(theme_get_accent() == (getenv("SONIX_UI_TEST_LEGACY") ? 0 : THEME_ACCENT_GRAPHITE));
		assert(theme_dynamic_tint() == (getenv("SONIX_UI_TEST_LEGACY") != NULL));
		printf("UI_PREFERENCES_PASS saved accent and tint retained\n");
		theme_set_accent(THEME_ACCENT_GRAPHITE);
		theme_set_dynamic_tint(false);
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		build_rows();
		switch_screen(main_menu_screen);
		break;
	case 1:
		capture("home-idle");
		{
			lv_obj_t *label = find_label(main_menu_screen, tr("music"));
			assert(label);
			lv_obj_t *tile = lv_obj_get_parent(label);
			lv_area_t t, l;
			lv_obj_get_coords(tile, &t);
			lv_obj_get_coords(label, &l);
			assert(abs(l.y2 - (t.y2 - 20 - 24 - 4)) <= 1);
			assert(lv_obj_get_width(tile) == 218);
			assert(lv_obj_get_width(lv_obj_get_child(tile, 0)) == 34);
			printf("UI_TILE_GEOMETRY_PASS prototype title baseline, subtitle slot and icon size\n");
		}
		sx_mini_set_track("Time", "/fixture/Time.wav", "Pink Floyd", false);
		sx_mini_set_playing(false);
		{
			lv_obj_t *title = find_label(lv_layer_top(), "Time");
			assert(title);
			lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(title));
			lv_obj_t *line = lv_obj_get_child(card, -1);
			lv_obj_t *fill = lv_obj_get_child(line, 0);
			sx_mini_set_progress(30, 120);
			lv_obj_update_layout(card);
			int width = lv_obj_get_width(fill);
			assert(width > 0);
			sx_mini_set_text("Late service title", "Late service artist");
			lv_obj_update_layout(card);
			assert(lv_obj_get_width(fill) == width && !lv_obj_is_hidden(fill));
			assert(find_label(card, "Late service title") && find_label(card, "Late service artist"));
			sx_mini_set_text("Time", "Pink Floyd");
			sx_mini_set_progress(0, 0);
			printf("UI_LATE_METADATA_PASS mini title update preserves progress\n");
		}
		if (getenv("SONIX_UI_TEST_FORMS_ONLY"))
			phase = 39 + 4 * (int)(sizeof(page_audit) / sizeof(page_audit[0])) + 15;
		if (getenv("SONIX_UI_TEST_CUSTOM_ONLY"))
			phase = 39;
		break;
	case 2:
		capture("home-mini");
		switch_screen(music_screen);
		break;
	case 3:
		capture("music");
		switch_screen(appearance_screen);
		break;
	case 4:
		capture("appearance");
		switch_screen(settings_screen);
		break;
	case 5:
		capture("settings");
		switch_screen(zvuk_screen);
		break;
	case 6:
		capture("zvuk-login");
		switch_screen(rows_screen);
		break;
	case 7:
		capture("rows-dark");
		sx_row_set_playing(track_row, true);
		capture("rows-playing");
		sx_row_set_playing(track_row, false);
		assert(lv_obj_is_hidden(sx_row_trail(track_row)));
		assert(!lv_obj_is_hidden(sx_row_trail(queue_row)));
		swipe(180, true);
		assert(likes == 1 && nexts == 0 && ends == 0 && taps == 0);
		swipe(180, false);
		assert(likes == 1 && nexts == 1 && ends == 0 && taps == 0);
		swipe(450, true);
		assert(ends == 1 && likes == 1);
		sx_row_set_trail(track_row, SX_ROW_TRAIL_CHECK);
		sx_row_set_swipes(track_row, false, false);
		swipe(180, true);
		assert(likes == 1 && ends == 1);
		theme_set_dark(false);
		break;
	case 8:
		capture("rows-light");
		switch_screen(appearance_screen);
		break;
	case 9:
		capture("appearance-light");
		switch_screen(main_menu_screen);
		break;
	case 10:
		capture("home-light");
		if (getenv("SONIX_UI_TEST_TRACK")) {
			const char *paths[] = {getenv("SONIX_UI_TEST_TRACK"), getenv("SONIX_UI_TEST_NEXT")};
			player_play_file(paths[0]);
			playlist_load_paths(paths, 2, 0);
			player_queue_changed();
		}
		player_sheet_open(false);
		break;
	case 11: {
		player_home_gestures();
		capture("player-light");
		lv_obj_t *label = find_label(player_screen, "Echoes");
		assert(label);
		lv_obj_t *chip = lv_obj_get_parent(lv_obj_get_parent(label));
		assert(lv_obj_get_width(chip) <= 344 && lv_obj_get_height(chip) == 72);
		lv_obj_add_state(chip, LV_STATE_PRESSED);
		capture("upnext-pressed");
		lv_obj_remove_state(chip, LV_STATE_PRESSED);
		theme_set_dark(true);
		break;
	}
	case 12:
		player_home_gestures();
		capture("player-dark");
		btline_set_enabled(false);
		player_chain_open();
		break;
	case 13:
		capture("audio-chain");
		{
			lv_obj_t *key = find_label(player_screen, tr("np_chain_file"));
			assert(key && !lv_obj_is_hidden(lv_obj_get_parent(lv_obj_get_parent(key))));
		}
		btline_set_enabled(true);
		player_sheet_close(false);
		medialist_open("Tracks", LIBRARY_LIST_TRACKS, LIBRARY_FILTER_NONE, NULL);
		break;
	case 14:
		capture("library-tracks");
		medialist_set_album_head(true);
		open_fixture_album(getenv("SONIX_UI_TEST_TRACK"), "The Dark Side of the Moon");
		break;
	case 15:
		capture("album-dark");
		theme_set_dark(false);
		break;
	case 16:
		capture("album-light");
		search_open();
		break;
	case 17:
		capture("search");
		{
			lv_obj_t *field = find_field(search_screen);
			assert(field);
			lv_textarea_set_text(field, "Pink Floyd");
			int height = lv_display_get_vertical_resolution(lv_display_get_default());
			input(438, height - 40, true);
			input(438, height - 40, false);
			capture("search-results");
			lv_obj_t *all = find_label(search_screen, tr("lists_filter_all"));
			assert(visible(all));
			lv_obj_t *filters = lv_obj_get_parent(lv_obj_get_parent(all));
			lv_area_t filters_box;
			lv_obj_get_coords(filters, &filters_box);
			assert(lv_obj_get_height(filters) == 52);
			assert(lv_obj_get_child_count(filters) == 4);
			for (uint32_t i = 0; i < 4; i++) {
				lv_obj_t *category = lv_obj_get_child(filters, (int)i);
				lv_area_t category_box;
				lv_obj_get_coords(category, &category_box);
				assert(category_box.y1 == filters_box.y1 && category_box.y2 <= filters_box.y2);
			}
			const char *titles[] = {"Time", "Echoes"};
			lv_obj_t *results_view = NULL;
			for (int i = 0; i < 2; i++) {
				lv_obj_t *label = find_label(search_screen, titles[i]);
				assert(visible(label));
				lv_area_t title_box;
				lv_obj_get_coords(label, &title_box);
				assert(title_box.y1 > filters_box.y2);
				if (!results_view) {
					results_view = label;
					while (lv_obj_get_parent(results_view) && lv_obj_get_parent(results_view) != search_screen)
						results_view = lv_obj_get_parent(results_view);
					assert(lv_obj_get_parent(results_view) == search_screen);
				}
			}
			lv_area_t box;
			lv_obj_get_coords(field, &box);
			input(180, (box.y1 + box.y2) / 2, true);
			input(180, (box.y1 + box.y2) / 2, false);
			capture("search-typing-results");
			assert(!visible(all));
			for (int i = 0; i < 2; i++) {
				lv_obj_t *label = find_label(search_screen, titles[i]);
				assert(label);
				lv_obj_t *r = label;
				while (r && lv_obj_get_height(r) != SX_ROW_H)
					r = lv_obj_get_parent(r);
				assert(r);
				lv_obj_get_coords(r, &box);
				lv_area_t field_box;
				lv_obj_get_coords(field, &field_box);
				assert(box.y1 > field_box.y2);
				assert(box.y2 < height - 316);
			}
			// A recent query follows a different callback, with existing rows.
			lv_obj_t *clear = find_image(search_screen, &icon_clear);
			assert(clear);
			lv_obj_get_coords(lv_obj_get_parent(clear), &box);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, false);
			capture("search-recent");
			lv_obj_t *recent_head = find_label(search_screen, tr("lists_head_recent"));
			assert(visible(recent_head));
			assert(lv_obj_get_content_height(recent_head) >= lv_font_get_line_height(lv_obj_get_style_text_font(recent_head, 0)));
			lv_obj_t *recent = find_label(lv_obj_get_parent(recent_head), "Pink Floyd");
			assert(visible(recent));
			lv_obj_get_coords(lv_obj_get_parent(recent), &box);
			theme_set_dark(true);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, false);
			capture("search-recent-results-dark");
			assert(visible(all));
			lv_obj_get_coords(filters, &filters_box);
			for (int i = 0; i < 2; i++) {
				lv_obj_t *label = find_label(search_screen, titles[i]);
				assert(visible(label));
				lv_obj_get_coords(label, &box);
				assert(box.y1 > filters_box.y2);
			}

			// A submitted category is part of its recent query, including when
			// the horizontal bar was scrolled somewhere else while hidden.
			lv_obj_t *album_label = find_label(filters, tr("albums"));
			lv_obj_t *track_label = find_label(filters, tr("search_tracks_2"));
			lv_obj_t *artist_label = find_label(filters, tr("artists"));
			assert(album_label && track_label && artist_label);
			lv_obj_t *albums = lv_obj_get_parent(album_label);
			lv_obj_t *tracks = lv_obj_get_parent(track_label);
			lv_obj_t *artists = lv_obj_get_parent(artist_label);
			lv_obj_send_event(albums, LV_EVENT_CLICKED, NULL);
			capture("search-albums");
			assert(visible(find_label(results_view, "The Dark Side")));
			assert(!visible(find_label(results_view, "Time")));
			assert(lv_color_eq(lv_obj_get_style_bg_color(albums, 0), sx_pal()->accent));
			lv_obj_send_event(lv_obj_get_parent(clear), LV_EVENT_CLICKED, NULL);
			lv_obj_update_layout(search_screen);
			assert(!visible(filters) && !visible(results_view));
			lv_obj_scroll_to_x(filters, 0, LV_ANIM_OFF);
			recent = find_label(lv_obj_get_parent(recent_head), "Pink Floyd");
			assert(visible(recent));
			lv_obj_send_event(lv_obj_get_parent(recent), LV_EVENT_CLICKED, NULL);
			capture("search-recent-albums");
			assert(strcmp(lv_textarea_get_text(field), "Pink Floyd") == 0);
			assert(visible(find_label(results_view, "The Dark Side")));
			assert(!visible(find_label(results_view, "Time")));
			assert(lv_color_eq(lv_obj_get_style_bg_color(albums, 0), sx_pal()->accent));
			lv_obj_get_coords(filters, &filters_box);
			lv_obj_get_coords(albums, &box);
			assert(box.x1 >= filters_box.x1 && box.x2 <= filters_box.x2);
			// The last category can begin beyond the horizontal viewport. A
			// replay must reveal it even after another scroll position was left.
			lv_obj_send_event(artists, LV_EVENT_CLICKED, NULL);
			lv_obj_send_event(lv_obj_get_parent(clear), LV_EVENT_CLICKED, NULL);
			lv_obj_scroll_to_x(filters, 0, LV_ANIM_OFF);
			recent = find_label(lv_obj_get_parent(recent_head), "Pink Floyd");
			assert(visible(recent));
			lv_obj_send_event(lv_obj_get_parent(recent), LV_EVENT_CLICKED, NULL);
			capture("search-recent-artists-revealed");
			assert(lv_color_eq(lv_obj_get_style_bg_color(artists, 0), sx_pal()->accent));
			assert(visible(find_label(results_view, "Pink Floyd")));
			lv_obj_get_coords(filters, &filters_box);
			lv_obj_get_coords(artists, &box);
			assert(box.x1 >= filters_box.x1 && box.x2 <= filters_box.x2);
			lv_obj_send_event(albums, LV_EVENT_CLICKED, NULL);

			// Time has a track hit and no album hit in the local fixture. Empty
			// Albums must retain the category controls and allow switching out.
			lv_obj_send_event(field, LV_EVENT_CLICKED, NULL);
			lv_textarea_set_text(field, "Time");
			lv_obj_update_layout(search_screen);
			input(438, height - 40, true);
			input(438, height - 40, false);
			capture("search-empty-albums");
			assert(visible(filters) && !visible(results_view));
			assert(visible(find_label(search_screen, tr("lists_nothing_found"))));
			assert(lv_color_eq(lv_obj_get_style_bg_color(albums, 0), sx_pal()->accent));
			lv_obj_send_event(tracks, LV_EVENT_CLICKED, NULL);
			capture("search-time-tracks");
			assert(visible(results_view) && visible(find_label(results_view, "Time")));
			assert(!visible(find_label(search_screen, tr("lists_nothing_found"))));
			assert(lv_color_eq(lv_obj_get_style_bg_color(tracks, 0), sx_pal()->accent));

			// Search, field and history use the same normalized query; a blank
			// submission returns to recents without recording a whitespace query.
			lv_obj_send_event(field, LV_EVENT_CLICKED, NULL);
			lv_textarea_set_text(field, "   Pink Floyd   ");
			lv_obj_update_layout(search_screen);
			input(438, height - 40, true);
			input(438, height - 40, false);
			capture("search-trimmed-query");
			assert(strcmp(lv_textarea_get_text(field), "Pink Floyd") == 0);
			assert(strcmp(config_get_copy("library", "search_recent_0", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "Pink Floyd") == 0);
			assert(visible(find_label(results_view, "Time")) && visible(find_label(results_view, "Echoes")));
			lv_obj_send_event(field, LV_EVENT_CLICKED, NULL);
			lv_textarea_set_text(field, "   ");
			lv_obj_update_layout(search_screen);
			input(438, height - 40, true);
			input(438, height - 40, false);
			capture("search-blank-query");
			assert(strcmp(lv_textarea_get_text(field), "") == 0);
			assert(!visible(filters) && !visible(results_view) && visible(recent_head));
			assert(strcmp(config_get_copy("library", "search_recent_0", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "Pink Floyd") == 0);

			// The remove control is adjacent to the query button. Exercise real
			// pointer hit testing: deleting Time must not replay its search.
			recent = find_label(lv_obj_get_parent(recent_head), "Time");
			assert(visible(recent));
			lv_obj_t *recent_row = lv_obj_get_parent(lv_obj_get_parent(recent));
			lv_obj_t *recent_list = lv_obj_get_parent(recent_row);
			uint32_t before_remove = lv_obj_get_child_count(recent_list);
			lv_obj_t *remove_icon = find_image(recent_row, &icon_clear);
			assert(visible(remove_icon));
			lv_obj_get_coords(lv_obj_get_parent(remove_icon), &box);
			assert(box.y2 < height - 316);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
			input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, false);
			capture("search-remove-recent");
			assert(lv_obj_get_child_count(recent_list) + 1 == before_remove);
			assert(!find_label(recent_list, "Time") && visible(find_label(recent_list, "Pink Floyd")));
			assert(strcmp(lv_textarea_get_text(field), "") == 0);
			assert(!visible(filters) && !visible(results_view) && visible(recent_head));
			theme_set_dark(false);
			printf("UI_SEARCH_REFLOW_PASS keyboard, horizontal categories, recent category, empty results, normalization and removal\n");
		}
		switch_screen(streaming_screen);
		break;
	case 18:
		capture("streaming-light");
		if (getenv("SONIX_UI_TEST_ZVUK")) {
			assert(strcmp(config_get_copy("zvuk", "base_url", "", (char[CONFIG_VALUE_SIZE]){0}, CONFIG_VALUE_SIZE), "http://127.0.0.1:18766") == 0);
			char message[256];
			assert(zvuk_sign_in("a1b2c3d4e5f60718293a4b5c6d7e8f90", message, sizeof(message)) == CARDIMPORT_OK);
		}
		switch_screen(zvuk_screen);
		break;
	case 19:
		break; // allow the service's normal worker to publish its result
	case 20:
		capture("zvuk-connected-light");
		if (getenv("SONIX_UI_TEST_ZVUK"))
			assert(zvukpage_open_album("701", "Группа крови"));
		break;
	case 21:
		break;
	case 22:
		capture("zvuk-album-light");
		theme_set_dark(true);
		break;
	case 23:
		capture("zvuk-album-dark");
		player_play_file(getenv("SONIX_UI_TEST_NEXT"));
		player_sheet_open(false);
		break;
	case 24:
		capture("player-cover");
		break;
	case 25:
		capture("player-standard-dark");
		theme_set_dark(false);
		capture("player-standard-light");
		break;
	case 26:
		capture("player-standard-light-restored");
		theme_set_dark(true);
		break;
	case 27:
		capture("player-standard-restored");
		{
			static char description[3000];
			for (int i = 0; i < 12; i++)
				strcat(description, "The service could not complete this request. Your library and downloads remain available. Check the connection and try again.\n\n");
			sx_sheet_t sheet = {.title = "Звук недоступен", .subtitle = description, .action_label = "Повторить", .cancel_label = "Закрыть"};
			sx_sheet_open(&sheet);
		}
		break;
	case 28: {
		capture("error-long");
		lv_obj_t *action = find_label(lv_layer_top(), "Повторить");
		assert(action);
		lv_area_t area;
		lv_obj_get_coords(lv_obj_get_parent(action), &area);
		assert(area.y2 < lv_display_get_vertical_resolution(lv_display_get_default()) && area.y2 - area.y1 + 1 >= 88);
		sx_sheet_close();
		break;
	}
	case 29:
		sx_toast_undo("Добавлено в избранное: The Great Gig in the Sky", undo, NULL);
		break;
	case 30: {
		capture("toast-undo");
		lv_obj_t *label = find_label(lv_layer_top(), tr("sx_undo"));
		assert(label);
		lv_obj_get_coords(lv_obj_get_parent(label), &undo_area);
		input((undo_area.x1 + undo_area.x2) / 2, (undo_area.y1 + undo_area.y2) / 2, true);
		break;
	}
	case 31:
	case 32:
	case 33:
	case 34:
	case 35:
	case 36:
	case 37:
		assert(sx_toast_showing());
		break;
	case 38:
		input((undo_area.x1 + undo_area.x2) / 2, (undo_area.y1 + undo_area.y2) / 2, false);
		assert(undos == 1 && !sx_toast_showing());
		player_sheet_close(false);
		if (getenv("SONIX_UI_TEST_CORE_ONLY"))
			phase = 39 + 4 * (int)(sizeof(page_audit) / sizeof(page_audit[0]));
		break;
	default: {
		int step = phase - 40;
		int count = getenv("SONIX_UI_TEST_CUSTOM_ONLY") ? 5 : (int)(sizeof(page_audit) / sizeof(page_audit[0]));
		int index = step / 4;
		int state = step % 4;
		if (index >= count) {
			switch (step - count * 4) {
			case 0:
				theme_set_dark(true);
				assert(fonts_set_large_text(true));
				switch_screen(main_menu_screen);
				return;
			case 1:
				capture("large-home");
				switch_screen(appearance_screen);
				return;
			case 2:
				capture("large-appearance");
				assert(fonts_set_large_text(false));
				medialist_set_album_head(false);
				open_fixture_album(getenv("SONIX_UI_TEST_TRACK"), "The Dark Side of the Moon");
				return;
			case 3:
				capture("album-compact");
				medialist_set_album_head(true);
				assert(fonts_set_large_text(true));
				return;
			case 4:
				capture("large-album");
				assert(fonts_set_large_text(false));
				return;
			case 5: {
				char path[512];
				snprintf(path, sizeof(path), "%s/Graphite/Audit-01.flac", getenv("SONIX_SD_ROOT"));
				open_fixture_album(path, "Artwork audit");
				return;
			}
			case 6: {
				capture("album-real-artwork");
				int w, h;
				uint16_t *pixels = display_capture_frame(&w, &h);
				assert(pixels && w == 480);
				assert(pixels[132 * w + 16] == pixels[132 * w + 8]);
				assert(pixels[180 * w + 48] != pixels[132 * w + 8]);
				uint16_t brown = pixels[180 * w + 48], blue = pixels[180 * w + 130];
				assert((brown >> 11) > 18 && ((brown >> 5) & 63) > 20 && (brown & 31) < 14);
				assert((blue >> 11) < 14 && ((blue >> 5) & 63) > 18 && (blue & 31) > 10);
				lv_obj_t *scroller = NULL;
				for (uint32_t i = 0; i < lv_obj_get_child_count(medialist_tracks_screen); i++) {
					lv_obj_t *child = lv_obj_get_child(medialist_tracks_screen, (int)i);
					if (lv_obj_is_scrollable(child) && lv_obj_get_height(child) > 300 && lv_obj_get_width(child) >= 400) {
						scroller = child;
						break;
					}
				}
				assert(scroller);
				lv_obj_scroll_to_y(scroller, 420, LV_ANIM_OFF);
				return;
			}
			case 7: {
				capture("album-sticky-header");
				int w, h;
				uint16_t *pixels = display_capture_frame(&w, &h);
				assert(pixels);
				lv_obj_t *heading = settingsrow_page_title(medialist_tracks_screen);
				assert(heading && visible(heading));
				assert(strcmp(lv_label_get_text(heading), "Artwork audit") == 0);
				lv_area_t heading_box;
				lv_obj_get_coords(heading, &heading_box);
				assert(heading_box.y1 >= SX_STATUSBAR_H && heading_box.y2 < SX_BODY_TOP);
				for (int x = 90; x < 450; x++)
					assert(pixels[130 * w + x] == pixels[130 * w + 10]);
				printf("UI_ALBUM_ART_PASS rounded loaded artwork and opaque scrolling header\n");
				return;
			}
			case 8:
				coverflow_set_enabled(true);
				switch_screen(music_screen);
				coverflow_open();
				return;
			case 9:
				capture("coverflow");
				input(240, lv_display_get_vertical_resolution(lv_display_get_default()) / 2, true);
				input(240, lv_display_get_vertical_resolution(lv_display_get_default()) / 2, false);
				return;
			case 10:
				capture("coverflow-tracks");
				input(8, 300, true);
				input(8, 300, false);
				return;
			case 11: {
				lv_obj_t *sheet = find_label(lv_layer_top(), "Artwork audit");
				assert(sheet);
				while (lv_obj_get_parent(sheet) != lv_layer_top())
					sheet = lv_obj_get_parent(sheet);
				lv_obj_t *grip = find_image(sheet, &icon_control_center_line);
				assert(grip);
				lv_obj_t *target = lv_obj_get_parent(grip);
				lv_area_t box;
				lv_obj_get_coords(target, &box);
				int x = (box.x1 + box.x2) / 2, y = (box.y1 + box.y2) / 2;
				printf("UI_CAROUSEL_GRIP %d,%d to %d,%d\n", box.x1, box.y1, box.x2, box.y2);
				input(x, y, true);
				input(x, y + 20, true);
				input(x, y + 180, true);
				input(x, y + 450, true);
				input(x, y + 450, false);
				return;
			}
			case 12:
				capture("coverflow-dismissed");
				{
					lv_obj_t *sheet = find_label(lv_layer_top(), "Artwork audit");
					assert(sheet);
					while (lv_obj_get_parent(sheet) != lv_layer_top())
						sheet = lv_obj_get_parent(sheet);
					assert(lv_obj_is_hidden(sheet));
				}
				return;
			case 13: {
				char path[512];
				snprintf(path, sizeof(path), "%s/Ebook/Graphite.epub", getenv("SONIX_SD_ROOT"));
				ebookreader_open(path);
				return;
			}
			case 14:
				capture("ebook-reader");
				return;
			case 15:
				switch_screen(main_menu_screen);
				quickpanel_open();
				return;
			case 16:
				capture("control-center-dark");
				assert(quickpanel_is_open());
				quickpanel_drag_begin();
				quickpanel_drag_update(-150);
				quickpanel_drag_end();
				assert(!quickpanel_is_open());
				return;
			case 17:
				theme_set_dark(false);
				quickpanel_open();
				return;
			case 18: {
				capture("control-center-light");
				lv_obj_t *icon = find_image(lv_layer_top(), &icon_wifi);
				assert(icon);
				lv_obj_t *label = lv_obj_get_child(lv_obj_get_parent(lv_obj_get_parent(icon)), 1);
				lv_area_t box;
				lv_obj_get_coords(label, &box);
				input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
				return;
			}
			case 19:
				input(point.x, point.y, false);
				assert(lv_screen_active() == wifisettings_screen && !quickpanel_is_open());
				capture("control-center-held-wifi");
				playlistpage_open();
				return;
			case 20:
				input(180, 196, true);
				input(180, 196, false);
				return;
			case 21: {
				capture("playlist-name-form");
				lv_obj_t *field = find_field(playlistpage_screen);
				assert(visible(field));
				lv_area_t box;
				lv_obj_get_coords(field, &box);
				assert(box.y1 == SX_BODY_TOP && box.y2 - box.y1 + 1 == 88);
				assert(keyboard_shown_on(playlistpage_screen) && !sx_mini_visible());
				lv_textarea_set_text(field, "Название для проверки переноса и отмены");
				input(436, 88, true);
				input(436, 88, false);
				return;
			}
			case 22:
				assert(!keyboard_shown_on(playlistpage_screen));
				filespage_open();
				return;
			case 23:
				input(436, 88, true);
				input(436, 88, false);
				return;
			case 24:
				capture("folder-name-form");
				assert(keyboard_shown_on(filespage_screen) && !sx_mini_visible());
				input(436, 88, true);
				input(436, 88, false);
				return;
			case 25:
				assert(!keyboard_shown_on(filespage_screen));
				welcome_show_first_boot();
				return;
			case 26:
				capture("first-boot");
				return;
			}
			printf("UI_TEST_PASS native scenario checks; %d extra pages in both themes\n", (getenv("SONIX_UI_TEST_CORE_ONLY") || getenv("SONIX_UI_TEST_FORMS_ONLY")) ? 0 : count);
			fflush(stdout);
			exit(0);
		}
		if (state == 0 || state == 2) {
			theme_set_dark(state == 0);
			if (*page_audit[index].screen == filespage_screen)
				filespage_open();
			else if (*page_audit[index].screen == ebookpage_screen)
				ebookpage_open();
			else
				switch_screen(*page_audit[index].screen);
		} else {
			char name[96];
			snprintf(name, sizeof(name), "audit-%s-%s", page_audit[index].name, state == 1 ? "dark" : "light");
			capture(name);
		}
		break;
	}
	}
}
void graphite_ui_test_init(void) {
	signal(SIGABRT, SIG_DFL); // test assertions must exit, not enter the device crash handler
	out_dir = getenv("SONIX_UI_TEST_DIR");
	if (out_dir && out_dir[0]) {
		// fflush() only hands text to the asynchronous log queue.
		// Drain it before a successful test exits and its marker is checked.
		if (atexit(logging_flush) != 0) {
			fprintf(stderr, "UI_TEST_ERROR: cannot register exit log drain\n");
			logging_flush();
			exit(EXIT_FAILURE);
		}
		lv_timer_create(run, 1000, NULL);
	}
}
