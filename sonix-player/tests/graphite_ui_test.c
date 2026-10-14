// Build with make host UI_TEST=1. Run only with disposable config and media roots.
// Uses LVGL's own input and rendering API; no desktop automation is required.
#include "src/system/core/logging.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "src/gui/nowplaying/btline.h"
#include "src/gui/nowplaying/coverflow.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/settings/appearance.h"
#include "src/gui/settings/ccsettings.h"
#include "src/gui/settings/devoptions.h"
#include "src/gui/settings/language.h"
#include "src/gui/settings/musicsettings.h"
#include "src/gui/settings/powersettings.h"
#include "src/gui/settings/remap.h"
#include "src/gui/settings/settings.h"
#include "src/gui/settings/systempage.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/main_menu.h"
#include "src/gui/shell/morepage.h"
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
#include "src/gui/streaming/tidalpage.h"
#include "src/gui/streaming/yandexpage.h"
#include "src/gui/streaming/zvukpage.h"
#include "src/gui/wireless/wifisettings.h"
#include "src/gui/wireless/wifitransfer.h"
#include "src/gui/wireless/wireless.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/device/power.h"
#include "src/system/image/png_write.h"
#include "src/system/playback/playlist.h"
#include "src/system/streaming/zvuk.h"

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

static void open_fixture_album(const char *path, const char *title) {
	char value[768];
	assert(library_track_album_value(path, value, sizeof(value)));
	medialist_open(title, LIBRARY_LIST_TRACKS, LIBRARY_FILTER_ALBUM, value);
}

static void mock_mode(const char *mode) {
	assert(strcmp(config_get("zvuk", "base_url", ""), "http://127.0.0.1:18766") == 0);
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
		printf("UI_TEST_PASS service states: expired, subscription, faults and recovery\n");
		logging_flush();
		exit(0);
	}
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

static void run(lv_timer_t *timer) {
	(void)timer;
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
			lv_area_t box;
			lv_obj_get_coords(field, &box);
			input(180, (box.y1 + box.y2) / 2, true);
			input(180, (box.y1 + box.y2) / 2, false);
			capture("search-typing-results");
			const char *titles[] = {"Time", "Echoes"};
			for (int i = 0; i < 2; i++) {
				lv_obj_t *label = find_label(search_screen, titles[i]);
				assert(label);
				lv_obj_t *r = label;
				while (r && lv_obj_get_height(r) != SX_ROW_H)
					r = lv_obj_get_parent(r);
				assert(r);
				lv_obj_get_coords(r, &box);
				assert(box.y2 < height - 316);
			}
		}
		switch_screen(streaming_screen);
		break;
	case 18:
		capture("streaming-light");
		if (getenv("SONIX_UI_TEST_ZVUK")) {
			assert(strcmp(config_get("zvuk", "base_url", ""), "http://127.0.0.1:18766") == 0);
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
		player_layout_set(PLAYER_LAYOUT_ALTERNATIVE);
		break;
	case 25:
		capture("player-waveform");
		theme_set_dark(false);
		capture("player-waveform-light");
		player_layout_set(PLAYER_LAYOUT_STUDIO);
		break;
	case 26:
		capture("player-studio");
		theme_set_dark(true);
		player_layout_set(PLAYER_LAYOUT_STANDARD);
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
				for (int y = 50; y < 130; y++) {
					for (int x = 90; x < 450; x++)
						assert(pixels[y * w + x] == pixels[y * w + 10]);
				}
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
	if (out_dir && out_dir[0])
		lv_timer_create(run, 1000, NULL);
}
