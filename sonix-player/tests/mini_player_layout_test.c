// Actual mini/frame/settings components on LVGL; services and storage are inert.
#define _GNU_SOURCE
#include "lvgl/lvgl.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/settings/appearance.h"
#include "src/gui/shell/artview.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/sx_frame.h"
#include "src/gui/shell/sx_mini.h"
#include "src/gui/shell/sx_palette.h"
#include "src/gui/shell/sx_styles.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/topbar.h"
#include "src/system/core/config.h"
#include "src/system/image/png_write.h"
#include "src/system/playback/device_state.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Generated definitions require the LVGL/font types above.
// clang-format off
#include "actual_fonts.inc"
// clang-format on

static int height, initial_mode, opens, pauses, nexts, prevs, mode_hooks, config_saves, config_sets, body_changes;
static bool fake_keyboard, sheet, fake_dark = true, palette_previews;
static unsigned binds, detaches, allocations;
static lv_obj_t *bound_image;
static const char *destination;
static uint8_t *pixels;
static lv_indev_t *pointer;
static bool pointer_down;
static lv_point_t pointer_point;
static bool fake_seekable;
static unsigned fake_revision = 1, seek_commits;
static double committed_position;
lv_obj_t *player_screen;
lv_obj_t *main_menu_screen;

#include "actual_time_format.inc"
bool player_mini_seek_begin(player_mini_seek_t *preview) {
	if (!fake_seekable)
		return false;
	*preview = (player_mini_seek_t){.position = 77, .start = 0, .length = 258, .total = 258, .revision = fake_revision};
	return true;
}
bool player_mini_seek_valid(const player_mini_seek_t *preview) { return preview->revision == fake_revision; }
bool player_mini_seek_commit(const player_mini_seek_t *preview, double position) {
	if (!player_mini_seek_valid(preview))
		return false;
	seek_commits++;
	committed_position = position;
	return true;
}

void *__real_lv_malloc(size_t size);
void *__real_lv_realloc(void *pointer, size_t size);
void *__wrap_lv_malloc(size_t size) {
	allocations++;
	return __real_lv_malloc(size);
}
void *__wrap_lv_realloc(void *pointer, size_t size) {
	allocations++;
	return __real_lv_realloc(pointer, size);
}

long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	if (!strcmp(key, "mini_player"))
		return initial_mode == -100 ? fallback : initial_mode;
	return fallback;
}
bool config_get_bool(const char *section, const char *key, bool fallback) {
	(void)section;
	return !strcmp(key, "dark_theme") ? fake_dark : fallback;
}
void config_set_int(const char *section, const char *key, long value) {
	(void)section;
	if (!strcmp(key, "mini_player")) {
		initial_mode = (int)value;
		config_sets++;
	}
}
void config_set_bool(const char *section, const char *key, bool value) {
	(void)section;
	(void)key;
	(void)value;
}
bool config_save(void) {
	config_saves++;
	return true;
}

bool keyboard_shown_on(lv_obj_t *screen) {
	(void)screen;
	return fake_keyboard;
}
bool player_sheet_is_open(void) { return sheet; }
bool player_sheet_drag_active(void) { return false; }
bool switcher_back_drag_active(void) { return false; }
void switcher_attach_back_gesture(lv_obj_t *object) { (void)object; }
void switch_screen(lv_obj_t *screen) {
	assert(screen == player_screen);
	opens++;
	sheet = true;
	sx_mini_player_shown(true);
}
void player_screen_next(void) { nexts++; }
void player_screen_prev(void) { prevs++; }
void player_key_play_pause(void) { pauses++; }
void player_mini_mode_changed(void) {
	mode_hooks++;
	sx_mini_set_cover(NULL, 0);
}
void sx_toast_follow_slot(void) {}
bool sx_artview_bind(lv_obj_t *image, const lv_image_dsc_t *source, uint32_t generation) {
	(void)generation;
	binds++;
	bound_image = image;
	lv_image_set_src(image, source);
	return true;
}
void sx_artview_detach(lv_obj_t *image) {
	detaches++;
	if (bound_image == image)
		bound_image = NULL;
}
bool sx_artview_refresh(void) { return false; }
bool sx_artview_mode(bool on) {
	(void)on;
	return true;
}
void sx_artview_screen_will_show(lv_obj_t *screen) { (void)screen; }
void sx_row_refresh_content(lv_obj_t *screen) { (void)screen; }
lv_color_t sx_artview_gray_color(lv_color_t value) { return value; }
uint16_t albumtone_hash_hue(const char *name) {
	unsigned hash = 2166136261u;
	for (; name && *name; name++)
		hash = (hash ^ (unsigned char)*name) * 16777619u;
	return hash % 360;
}
const song_metadata_t *device_state_loaded_metadata(const char **file) {
	if (file)
		*file = NULL;
	return NULL;
}
const char *tr(const char *key) {
	if (!strcmp(key, "appearance_mini_player"))
		return "Mini-player";
	if (!strcmp(key, "appearance_mini_normal"))
		return "Normal";
	if (!strcmp(key, "appearance_mini_compact"))
		return "Compact";
	if (!strcmp(key, "appearance_mini_home_note"))
		return "На главной — обычный, в разделах — компактный.";
	return key;
}
void fonts_register_change(void (*callback)(void)) { (void)callback; }
bool fonts_large_text(void) { return false; }
bool fonts_set_large_text(bool large) {
	(void)large;
	return true;
}
bool touch_large_rows(void) { return false; }
void touch_set_large_rows(bool large) { (void)large; }
void touch_grow(lv_obj_t *object, int width, int target_height, int limit) {
	(void)object;
	(void)width;
	(void)target_height;
	(void)limit;
}
bool corners_enabled(void) { return false; }
void corners_set_enabled(bool enabled) { (void)enabled; }
bool player_statusbar_always(void) { return false; }
void player_set_statusbar_always(bool shown) { (void)shown; }
void topbar_set_battery_percent(bool shown) { (void)shown; }
void topbar_set_clock_position(int position) { (void)position; }
void main_menu_open_settings(void) {}
void bootlogo_follow_theme(bool dark) { (void)dark; }

#include "actual_mini.c"
#ifdef BASELINE_MINI
// Compatibility shims for the unchanged old mini; no parallel drawing/layout.
static lv_obj_t *compact_line;
static char title_text[512];
#define text_box lv_obj_get_parent(title)
int sx_mini_mode(void) { return SX_MINI_NORMAL; }
int sx_mini_slot_height(void) { return SX_MINI_SLOT_H; }
bool sx_mini_wants_cover(void) { return true; }
bool sx_mini_set_mode(int mode) {
	(void)mode;
	return false;
}
#endif

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data) {
	(void)input;
	data->point = pointer_point;
	data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void input(int x, int y, bool down) {
	pointer_point = (lv_point_t){x, y};
	pointer_down = down;
	lv_indev_read(pointer);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
	(void)area;
	(void)data;
	lv_display_flush_ready(display);
}
static void settle(void) {
	for (int i = 0; i < 10; i++) {
		lv_tick_inc(10);
		lv_timer_handler();
	}
	lv_obj_update_layout(lv_screen_active());
	lv_obj_update_layout(lv_layer_top());
	lv_refr_now(NULL);
}
static void capture(const char *name) {
	settle();
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.png", destination, name);
	assert(png_write_rgb565(path, (uint16_t *)pixels, 480, height));
}
static void close_sheet(void) {
	sheet = false;
	sx_mini_player_shown(false);
	settle();
}
static lv_area_t bounds(lv_obj_t *object) {
	lv_area_t area;
	lv_obj_get_coords(object, &area);
	return area;
}
static void body_changed(void) { body_changes++; }
static bool cover_tone(const char *path, float *hue, float *chroma) {
	(void)path;
	*hue = 31;
	*chroma = 0.15f;
	return true;
}
static lv_obj_t *find_label(lv_obj_t *parent, const char *text) {
	if (lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), text))
		return parent;
	for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
		lv_obj_t *found = find_label(lv_obj_get_child(parent, (int32_t)i), text);
		if (found)
			return found;
	}
	return NULL;
}
static void gesture(int dx, int dy) {
	lv_area_t area = bounds(slot);
	int x = 220, y = area.y1 + 30;
	input(x, y, true);
	input(x + dx, y + dy, true);
	input(x + dx, y + dy, false);
	settle();
}
static void check_geometry(int mode) {
	settle();
	lv_area_t band = bounds(slot), face = bounds(card), target = bounds(play_area), line = bounds(progress_track);
	int reserve = mode == SX_MINI_COMPACT ? 72 : 128;
	assert(lv_obj_get_height(slot) == reserve && band.y1 == height - reserve && band.y2 == height - 1);
	assert(lv_obj_get_height(card) == (mode == SX_MINI_COMPACT ? 56 : 112));
	assert(face.x1 == 16 && face.x2 == 463 && face.y1 == height - reserve + 8 && face.y2 == height - 9);
	printf("GEOM mode=%d band=%d..%d face=%d..%d target=%d..%d width=%d text=%d..%d\n", mode, band.y1, band.y2, face.y1, face.y2, target.y1, target.y2, lv_obj_get_width(play_area), bounds(text_box).y1, bounds(text_box).y2);
	fflush(stdout);
	assert(target.y1 >= band.y1 && target.y2 <= band.y2);
	assert(lv_obj_get_height(slot) >= 64);
	assert(lv_obj_get_width(play_area) == (mode == SX_MINI_COMPACT ? 64 : 88));
	assert(lv_obj_get_height(play_area) == (mode == SX_MINI_COMPACT ? 64 : 112));
	assert(line.x1 >= face.x1 + 18 && line.x2 <= face.x2 - 18 && line.y2 <= face.y2 - 4);
	assert(lv_obj_get_width(progress_track) == (mode == SX_MINI_COMPACT ? 354 : 412));
	assert(lv_obj_get_width(progress_fill) == (int)(progress_fraction * progress_w + 0.5));
	if (mode == SX_MINI_NORMAL) {
		lv_area_t art = bounds(cover), words = bounds(title), button = bounds(play_disc);
		assert(art.x1 == 28 && art.y1 == face.y1 + 12);
		assert(lv_obj_get_width(cover) == 88 && lv_obj_get_height(cover) == 88);
		assert(words.x1 == 128 && lv_obj_get_width(title) == 224);
		assert(target.x1 - words.x2 - 1 == 12);
		assert(art.y1 + art.y2 == button.y1 + button.y2);
		assert(lv_obj_get_style_text_font(title, 0) == &font_ui_24_bold);
		assert(lv_obj_get_style_text_font(artist, 0) == &font_ui_22);
	}
	assert(sx_frame_body_bottom() == reserve);
	if (mode == SX_MINI_NORMAL)
		assert(lv_obj_get_child(card, -1) == progress_track);
	assert(lv_obj_is_hidden(cover) == (mode == SX_MINI_COMPACT));
	assert(lv_obj_is_hidden(title) == (mode == SX_MINI_COMPACT));
	assert(lv_obj_is_hidden(compact_line) == (mode != SX_MINI_COMPACT));
}

static double pixel_luminance(lv_color_t c) {
	unsigned r = c.red >> 3, g = c.green >> 2, b = c.blue >> 3;
	double channels[] = {(double)((r << 3) | (r >> 2)) / 255, (double)((g << 2) | (g >> 4)) / 255, (double)((b << 3) | (b >> 2)) / 255};
	for (int i = 0; i < 3; i++)
		channels[i] = channels[i] <= .04045 ? channels[i] / 12.92 : pow((channels[i] + .055) / 1.055, 2.4);
	return .2126 * channels[0] + .7152 * channels[1] + .0722 * channels[2];
}

static double pixel_contrast(lv_color_t a, lv_color_t b) {
	double x = pixel_luminance(a), y = pixel_luminance(b);
	return (fmax(x, y) + .05) / (fmin(x, y) + .05);
}

int main(int argc, char **argv) {
	assert(argc == 5);
	height = atoi(argv[1]);
	initial_mode = atoi(argv[2]);
	destination = argv[3];
	fake_dark = strcmp(argv[4], "light") != 0;
	palette_previews = height == 800 && initial_mode == -100 && fake_dark;
	lv_init();
	char path[1024];
	snprintf(path, sizeof(path), "%s/ui-regular.ttf", getenv("SONIX_TEST_FONT_DIR"));
#include "load_actual_fonts.inc"
	lv_display_t *display = lv_display_create(480, height);
	lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
	pixels = calloc((size_t)480 * height, 2);
	assert(pixels);
	lv_display_set_buffers(display, pixels, NULL, 480 * height * 2, LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(display, flush);
	theme_init();
	gui_config_t cfg = {.screen_width = 480, .screen_height = height, .padding = 16, .top_bar_height = 44, .sd_root_path = "/fixture"};
	player_screen = lv_obj_create(lv_layer_top());
	lv_obj_set_hidden(player_screen, true);
	sx_frame_init(&cfg);
	sx_frame_on_body_change(body_changed);
	int expected = initial_mode == SX_MINI_COMPACT ? SX_MINI_COMPACT : SX_MINI_NORMAL;
	assert(sx_mini_mode() == expected && config_saves == 0);
	lv_obj_t *screen = lv_screen_active();
	// Streaming lists create an empty heading before their data arrives.
	lv_obj_t *dynamic_title = settingsrow_title(screen, &cfg, "");
	settingsrow_title_corner_slots(dynamic_title, &cfg, 1);
	lv_label_set_text(dynamic_title, "Search results");
	lv_obj_update_layout(screen);
	lv_area_t dynamic_bounds = bounds(dynamic_title);
	fprintf(stderr, "dynamic header bounds: %d,%d..%d,%d\n", dynamic_bounds.x1, dynamic_bounds.y1, dynamic_bounds.x2, dynamic_bounds.y2);
	assert(dynamic_bounds.x1 >= SX_BODY_PAD_L && dynamic_bounds.y1 >= SX_STATUSBAR_H && dynamic_bounds.y2 < SX_BODY_TOP);
	lv_obj_delete(dynamic_title);
	lv_obj_t *scroller = settingsrow_page(screen, &cfg, "Settings");
	lv_obj_t *last = NULL;
	for (int i = 0; i < 14; i++)
		last = settingsrow_add(scroller, "A setting with its normal touch target", NULL, NULL, NULL);
	sx_frame_page_shown(screen);
	sx_mini_set_track("A long title with several words", "/fixture/local.wav", "Artist", false);
	sx_mini_set_playing(true);
	pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_pointer);
	lv_timer_pause(lv_indev_get_read_timer(pointer));
#ifdef BASELINE_MINI
	settle();
	lv_obj_scroll_to_y(scroller, lv_obj_get_scroll_y(scroller) + lv_obj_get_scroll_bottom(scroller), LV_ANIM_OFF);
	settle();
	capture("normal-settings-last-row");
	return 0;
#endif
	for (int mode = 0; mode < 2; mode++) {
		sx_mini_set_mode(mode);
		check_geometry(mode);
		lv_obj_scroll_to_y(scroller, lv_obj_get_scroll_y(scroller) + lv_obj_get_scroll_bottom(scroller), LV_ANIM_OFF);
		settle();
		lv_area_t row = bounds(last);
		assert(row.y2 < height - sx_frame_body_bottom());
		capture(mode ? "compact-settings-last-row" : "normal-settings-last-row");
		int before = opens;
		gesture(0, -SX_MINI_SWIPE_UP_PX - 12);
		assert(opens == before + 1);
		close_sheet();
		before = nexts;
		gesture(-SX_MINI_SWIPE_PX - 12, 0);
		assert(nexts == before + 1);
		before = prevs;
		gesture(SX_MINI_SWIPE_PX + 12, 0);
		assert(prevs == before + 1);
		before = opens;
		gesture(0, -SX_MINI_SWIPE_UP_PX);
		assert(opens == before);
		before = nexts;
		gesture(-SX_MINI_SWIPE_PX, 0);
		assert(nexts == before);
		before = prevs;
		gesture(SX_MINI_SWIPE_PX, 0);
		assert(prevs == before);
		lv_area_t target = bounds(play_area);
		before = pauses;
		int open_before_pause = opens;
		input((target.x1 + target.x2) / 2, (target.y1 + target.y2) / 2, true);
		input((target.x1 + target.x2) / 2, (target.y1 + target.y2) / 2, false);
		assert(pauses == before + 1 && opens == open_before_pause);
		for (int step = 0; step <= 2; step++) {
			sx_mini_set_progress(step * 50, 100);
			settle();
			check_geometry(mode);
		}
		fake_keyboard = true;
		sx_mini_update_visibility();
		assert(!sx_mini_visible() && sx_frame_body_bottom() == 0);
		assert(!sx_mini_wants_cover());
		fake_keyboard = false;
		sx_mini_update_visibility();
		check_geometry(mode);
		sx_mini_set_text("", "");
		settle();
		capture(mode ? "compact-empty-metadata" : "normal-empty-metadata");
		sx_mini_set_track("Live station", NULL, "Concert", true);
		sx_mini_set_playing(true);
		assert(lv_image_get_src(play_icon) == &icon_stop);
		capture(mode ? "compact-radio" : "normal-radio");
		sx_mini_set_track("A long title with several words", "/fixture/local.wav", "Artist", false);
	}
	// Real palette/theme code, cover accent and tint; no thumbnail decoder runs.
	sx_palette_set_tone_provider(cover_tone);
	theme_set_accent(THEME_ACCENT_COVER);
	theme_set_dynamic_tint(true);
	sx_palette_track_changed("/fixture/local.wav", "Album");
	for (int family = 0; family < THEME_PALETTE_COUNT; family++) {
		theme_set_palette(family);
		for (int mode = 0; mode < 2; mode++) {
			sx_mini_set_mode(mode);
			for (int step = 0; step <= 2; step++) {
				sx_mini_set_progress(step * 50, 100);
				settle();
				check_geometry(mode);
			}
			assert(lv_color_eq(lv_obj_get_style_bg_color(card, 0), sx_pal()->mini_bg));
			assert(lv_color_eq(lv_obj_get_style_text_color(compact_line, 0), sx_pal()->mini_ink));
			assert(lv_color_eq(lv_obj_get_style_bg_color(play_disc, 0), sx_pal()->mini_action));
			assert(lv_obj_get_style_bg_opa(play_disc, 0) == LV_OPA_COVER);
			assert(lv_color_eq(lv_obj_get_style_image_recolor(play_icon, 0), sx_pal()->mini_on_action));
			assert(pixel_contrast(sx_pal()->mini_ink, sx_pal()->mini_bg) >= 4.5);
			assert(pixel_contrast(sx_pal()->mini_text2, sx_pal()->mini_bg) >= 4.5);
			assert(pixel_contrast(sx_pal()->mini_on_action, sx_pal()->mini_action) >= 3);
			if (palette_previews) {
				char name[64];
				snprintf(name, sizeof(name), "palette-%d-%s", family, mode ? "compact" : "normal");
				capture(name);
			}
		}
	}
	assert(!sx_mini_set_mode(SX_MINI_COMPACT));
	int saved = config_saves;
	sx_mini_set_text("Same", "Artist");
	settle();
	allocations = 0;
	for (int i = 0; i < 100; i++)
		sx_mini_set_text("Same", "Artist");
	assert(allocations == 0 && config_saves == saved);
	const char *long_title = "A very long title with more words than a small compact line can display, keeping its canonical metadata intact across ellipsis and display mode changes";
	sx_mini_set_text(long_title, "Artist");
	settle();
	allocations = 0;
	for (int i = 0; i < 100; i++)
		sx_mini_set_text(long_title, "Artist");
	assert(allocations == 0 && !strcmp(title_text, long_title));
	sx_mini_set_mode(SX_MINI_NORMAL);
	sx_mini_set_mode(SX_MINI_COMPACT);
	assert(!strcmp(title_text, long_title));
	input(220, height - 40, true);
	int before = opens;
	sx_mini_set_mode(SX_MINI_NORMAL);
	input(220, height - 40, false);
	assert(opens == before);
	sx_mini_set_mode(SX_MINI_COMPACT);
	settle();
	lv_area_t pressed_pause = bounds(play_area);
	before = pauses;
	int open_before_cancel = opens;
	input(pressed_pause.x1 + 20, pressed_pause.y1 + 20, true);
	sx_mini_set_mode(SX_MINI_NORMAL);
	input(pressed_pause.x1 + 20, pressed_pause.y1 + 20, false);
	assert(pauses == before && opens == open_before_cancel);
	assert(sx_mini_set_mode(99) == false && sx_mini_mode() == SX_MINI_NORMAL);
	// Actual Appearance callback through the existing pills, not a parallel setter.
	appearance_screen = lv_obj_create(NULL);
	appearance_init(&cfg);
	lv_screen_load(appearance_screen);
	sx_mini_update_visibility();
	sx_frame_page_shown(appearance_screen);
	settle();
	lv_obj_t *compact_option = find_label(appearance_screen, "Compact");
	assert(compact_option);
	saved = config_saves;
	lv_obj_t *compact_button = lv_obj_get_parent(compact_option);
	lv_obj_scroll_to_view_recursive(compact_button, LV_ANIM_OFF);
	settle();
	lv_area_t compact_hit = bounds(compact_button);
	fprintf(stderr, "APPEARANCE %d..%d reserve=%d\n", compact_hit.y1, compact_hit.y2, sx_frame_body_bottom());
	capture("appearance-before-tap");
	assert(compact_hit.y1 >= SX_BODY_TOP && compact_hit.y2 < height - sx_frame_body_bottom());
	input((compact_hit.x1 + compact_hit.x2) / 2, (compact_hit.y1 + compact_hit.y2) / 2, true);
	input((compact_hit.x1 + compact_hit.x2) / 2, (compact_hit.y1 + compact_hit.y2) / 2, false);
	assert(sx_mini_mode() == SX_MINI_COMPACT && config_saves == saved + 1);
	check_geometry(SX_MINI_COMPACT);
	lv_obj_t *home_note = find_label(appearance_screen, tr("appearance_mini_home_note"));
	assert(home_note && !lv_obj_is_hidden(home_note));
	settle();
	assert(lv_obj_get_height(home_note) >= 2 * lv_font_get_line_height(&font_ui_18));
	lv_area_t note_area = bounds(home_note), note_card = bounds(lv_obj_get_parent(home_note));
	assert(note_area.y2 < note_card.y2);
	lv_obj_t *normal_option = find_label(appearance_screen, "Normal");
	assert(normal_option);
	lv_obj_t *normal_button = lv_obj_get_parent(normal_option);
	lv_obj_scroll_to_view_recursive(normal_button, LV_ANIM_OFF);
	settle();
	lv_area_t normal_hit = bounds(normal_button);
	input((normal_hit.x1 + normal_hit.x2) / 2, (normal_hit.y1 + normal_hit.y2) / 2, true);
	input((normal_hit.x1 + normal_hit.x2) / 2, (normal_hit.y1 + normal_hit.y2) / 2, false);
	check_geometry(SX_MINI_NORMAL);
	// Hidden/input-overlay changes release only the mini-specific reserve.
	sx_mini_input_overlay(appearance_screen, true);
	assert(sx_frame_body_bottom() == 0);
	sx_mini_set_mode(SX_MINI_COMPACT);
	assert(sx_frame_body_bottom() == 0);
	sx_mini_input_overlay(appearance_screen, false);
	check_geometry(SX_MINI_COMPACT);
	lv_obj_t *hidden = lv_obj_create(NULL);
	sx_mini_hide_on_screen(hidden);
	lv_screen_load(hidden);
	sx_mini_update_visibility();
	assert(sx_frame_body_bottom() == 0);
	lv_screen_load(appearance_screen);
	sx_mini_update_visibility();
	check_geometry(SX_MINI_COMPACT);
	sx_mini_set_track(NULL, NULL, NULL, false);
	assert(sx_frame_body_bottom() == 0 && !sx_mini_visible());
	lv_obj_t *idle = lv_obj_create(appearance_screen);
	sx_frame_set_idle_card(appearance_screen, idle);
	settle();
	assert(lv_obj_get_height(idle) == SX_MINI_H && sx_frame_slot_height() == SX_MINI_SLOT_H);
	assert(sx_frame_body_bottom() == 0); // another footer is independent of mini reserve
	sx_mini_set_track("Track", "/fixture/local.wav", "Artist", false);
	check_geometry(SX_MINI_COMPACT);
	assert(lv_obj_is_hidden(idle) && body_changes > 8);
	// Home uses the normal face without changing the stored compact choice.
	int before_home_saves = config_saves;
	main_menu_screen = lv_obj_create(NULL);
	lv_screen_load(main_menu_screen);
	sx_mini_update_visibility();
	sx_frame_page_shown(main_menu_screen);
	check_geometry(SX_MINI_NORMAL);
	assert(sx_mini_mode() == SX_MINI_COMPACT && sx_mini_wants_cover());
	assert(config_saves == before_home_saves);
	capture("home-normal-with-compact-preference");
	lv_screen_load(appearance_screen);
	sx_mini_update_visibility();
	sx_frame_page_shown(appearance_screen);
	check_geometry(SX_MINI_COMPACT);
	assert(!sx_mini_wants_cover() && config_saves == before_home_saves);
	assert(home_note);
	// The new preview is allocated only on first use, and never seeks on move.
	assert(!seek_panel);
	fake_seekable = true;
	int y = height - SX_MINI_COMPACT_SLOT_H + 30;
	int old_nexts = nexts, old_prevs = prevs, old_opens = opens, old_pauses = pauses;
	input(220, y, true);
	lv_tick_inc(500);
	input(220, y, true);
	assert(seek_active && seek_panel && sx_frame_body_bottom() == SX_MINI_COMPACT_SLOT_H);
	assert(seek_target == 77 && seek_commits == 0);
	// The delta agrees with displayed whole-second times, without -0:00.
	seek_range.position = 77.9;
	seek_target = 78.1;
	seek_paint(220);
	assert(!strcmp(lv_label_get_text(seek_delta), "+0:01"));
	seek_target = 77.1;
	seek_paint(220);
	assert(!strcmp(lv_label_get_text(seek_delta), "+0:00"));
	seek_range.position = seek_target = 77;
	seek_paint(220);
	capture("compact-seek-hold");
	input(260, y, true);
	assert(seek_active && seek_target > 77 && seek_commits == 0);
	capture("compact-seek-preview");
	input(260, y, false);
	assert(!seek_active && seek_commits == 1 && committed_position > 77);
	assert(nexts == old_nexts && prevs == old_prevs && opens == old_opens && pauses == old_pauses);
	// Moving above the expanded region cancels and consumes the release.
	input(220, y, true);
	lv_tick_inc(500);
	input(220, y, true);
	assert(seek_active);
	input(220, height - SX_MINI_SLOT_H - 30, true);
	input(220, height - SX_MINI_SLOT_H - 30, false);
	assert(!seek_active && seek_commits == 1 && opens == old_opens);
	// A track-generation change cannot apply the old target to the new track.
	input(220, y, true);
	lv_tick_inc(500);
	input(220, y, true);
	assert(seek_active);
	fake_revision++;
	sx_mini_set_progress(0, 120);
	input(220, y, false);
	assert(!seek_active && seek_commits == 1 && opens == old_opens);
	check_geometry(SX_MINI_COMPACT);
	puts("PASS actual mini/frame LVGL geometry, settings last row, real pointer gestures, pause, keyboard, metadata/radio, persistence/defaults, unchanged text");
	return 0;
}
