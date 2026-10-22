// Production sx_row pointer dispatch and animation; no device or files.
#include "src/gui/shell/sx_row.c"
#include <assert.h>
lv_font_t font_ui_14, font_ui_20, font_ui_20_bold, font_ui_22, font_ui_26_bold;
static lv_indev_t *pointer;
static lv_point_t position;
static bool down;
static lv_obj_t *cancel_target;
static unsigned removes, taps, nexts;
static void *removed_user;
bool touch_large_rows(void) { return false; }
bool player_sheet_drag_active(void) { return false; }
bool switcher_back_drag_active(void) { return false; }
bool onscreen_covered(lv_obj_t *obj) {
	(void)obj;
	return false;
}
void power_pause_in_standby(lv_timer_t *timer) { (void)timer; }
const char *tr(const char *key) { return key; }
static void removed_cb(lv_obj_t *row, void *user) {
	(void)row;
	removes++;
	removed_user = user;
}
static void tapped_cb(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	taps++;
}
static void next_cb(lv_obj_t *row, void *user) {
	(void)row;
	(void)user;
	nexts++;
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data) {
	(void)area;
	(void)data;
	lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *data) {
	(void)i;
	data->point = position;
	data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	if (cancel_target) {
		lv_obj_t *target = cancel_target;
		cancel_target = NULL;
		assert(lv_indev_active());
		lv_obj_send_event(target, LV_EVENT_PRESS_LOST, NULL);
	}
}
static void input(int x, int y, bool pressed) {
	position = (lv_point_t){x, y};
	down = pressed;
	lv_indev_read(pointer);
}
static void advance(unsigned ms) {
	for (unsigned t = 0; t < ms; t += 10) {
		lv_tick_inc(10);
		lv_timer_handler();
	}
}
static void swipe(int from, int to, int y) {
	input(from, y, true);
	input(from + (to - from) / 2, y, true);
	input(to, y, true);
	input(to, y, false);
	advance(250);
}
int main(void) {
	lv_init();
	const char *fonts = getenv("SONIX_TEST_FONT_DIR");
	assert(fonts);
	char path[1024];
	snprintf(path, sizeof(path), "%s/ui-regular.ttf", fonts);
	const lv_font_t *f = lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 22, LV_FREETYPE_FONT_STYLE_NORMAL);
	assert(f);
	font_ui_14 = font_ui_20 = font_ui_20_bold = font_ui_22 = font_ui_26_bold = *f;
	lv_display_t *d = lv_display_create(480, 800);
	static uint16_t frame[480 * 800];
	lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(d, frame, NULL, sizeof(frame), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(d, flush);
	sx_styles_init();
	pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_pointer);
	lv_timer_pause(lv_indev_get_read_timer(pointer));
	lv_obj_t *page = lv_obj_create(lv_screen_active());
	lv_obj_remove_style_all(page);
	lv_obj_set_size(page, 480, 700);
	lv_obj_set_scroll_dir(page, LV_DIR_VER);
	lv_obj_t *spacer = lv_obj_create(page);
	lv_obj_set_pos(spacer, 0, 900);
	lv_obj_t *content;
	sx_row_handlers_t h = {.on_tap = tapped_cb, .on_swipe_end = removed_cb, .end_is_remove = true, .remove_label_key = "hometiles_remove"};
	lv_obj_t *row = sx_row_create_custom(page, &h, 72, &content);
	assert(row && content);
	lv_obj_set_size(row, 440, 72);
	lv_obj_set_pos(row, 20, 100);
	sx_row_set_user(row, (void *)7);
	lv_obj_update_layout(page);
	advance(50);
	swipe(320, 270, 130);
	assert(removes == 0 && taps == 0);
	swipe(320, 170, 130);
	assert(removes == 1 && removed_user == (void *)7 && taps == 0);
	assert(!strcmp(lv_label_get_text(row_of(row)->under_label), "hometiles_remove"));
	swipe(150, 320, 130);
	assert(removes == 1 && nexts == 0 && taps == 0);
	input(320, 130, true);
	sx_row_set_user(row, (void *)9);
	input(200, 130, true);
	input(170, 130, true);
	input(170, 130, false);
	advance(250);
	assert(removes == 2 && removed_user == (void *)7);
	input(320, 130, true);
	input(180, 130, true);
	cancel_target = content;
	input(180, 130, false);
	advance(250);
	assert(removes == 2 && lv_obj_is_scrollable(page));
	swipe(10, 180, 130);
	assert(removes == 2);
	input(320, 130, true);
	input(315, 170, true);
	input(180, 170, true);
	input(180, 170, false);
	advance(250);
	assert(removes == 2 && taps == 0);
	lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
	advance(20);
	unsigned before_taps = taps;
	input(280, 130, true);
	input(280, 130, false);
	assert(taps == before_taps + 1);
	// Existing non-custom next/remove rows retain their original handlers.
	h.on_swipe_next = next_cb;
	h.remove_label_key = NULL;
	lv_obj_t *normal = sx_row_create(page, &h, true);
	lv_obj_set_size(normal, 440, 72);
	lv_obj_set_pos(normal, 20, 230);
	sx_row_set_user(normal, (void *)11);
	advance(50);
	swipe(150, 320, 260);
	assert(nexts == 1);
	swipe(320, 170, 260);
	assert(removes == 3 && removed_user == (void *)11);
	assert(!strcmp(lv_label_get_text(row_of(normal)->under_label), "sx_row_remove"));
	puts("PASS production row gestures: short/cancel/vertical/back/right "
		 "disabled, press-ID snapshot, contextual label; existing next/remove "
		 "unchanged");
}
