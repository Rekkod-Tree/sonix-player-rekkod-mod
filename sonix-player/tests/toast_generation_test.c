// Production LVGL pointer regression. See tests/run_toast_generation.sh.
// Only external page state and translation are fixtures; sx_toast, styles,
// spinner, icons, rendering and pointer dispatch are production code.
#include "lvgl/lvgl.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/sx_styles.h"
#include "src/gui/shell/theme.h"
#include "src/system/image/png_write.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef TOAST_SOURCE
#define TOAST_SOURCE "../src/gui/shell/sx_toast.c"
#endif
#include TOAST_SOURCE

lv_font_t font_ui_14, font_ui_22, font_ui_22_bold;
lv_style_t theme_style_icon;
static bool fixture_player, fixture_slot = true;
bool player_sheet_is_open(void) { return fixture_player; }
bool sx_frame_slot_occupied(void) { return fixture_slot; }
const char *tr(const char *key) { return strcmp(key, "sx_undo") == 0 ? "Отменить" : key; }

static lv_indev_t *pointer;
static lv_point_t position;
static bool down;
static unsigned actions[2], underlay_clicks, underlay_presses, underlay_releases;
static uint16_t *frame;
static int height;
static const char *output;

static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data) {
	(void)area;
	(void)data;
	lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *data) {
	(void)i;
	data->point = position;
	data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void input(int x, int y, bool held_down) {
	position = (lv_point_t){x, y};
	down = held_down;
	lv_indev_read(pointer);
}
static void advance(unsigned ms) {
	for (unsigned elapsed = 0; elapsed < ms; elapsed += 10) {
		lv_tick_inc(10);
		lv_timer_handler();
	}
}
static lv_point_t middle(lv_obj_t *o) {
	lv_obj_update_layout(lv_layer_top());
	lv_area_t a;
	lv_obj_get_coords(o, &a);
	return (lv_point_t){(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}
static lv_point_t body(void) {
	lv_obj_update_layout(lv_layer_top());
	lv_area_t a;
	lv_obj_get_coords(toast, &a);
	return (lv_point_t){a.x1 + 40, (a.y1 + a.y2) / 2};
}
static void tap(lv_point_t p) {
	input(p.x, p.y, true);
	input(p.x, p.y, false);
}
static void action(void *user) { actions[(size_t)user]++; }
static void underlying(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_PRESSED)
		underlay_presses++;
	if (lv_event_get_code(e) == LV_EVENT_RELEASED)
		underlay_releases++;
	if (lv_event_get_code(e) == LV_EVENT_CLICKED)
		underlay_clicks++;
}
static void capture(const char *name) {
	lv_refr_now(NULL);
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.png", output, name);
	assert(png_write_rgb565(path, frame, 480, height));
}
static void normal(void) {
	sx_toast_undo("Ярлык убран с главного экрана", action, (void *)0);
	tap(body());
	assert(!sx_toast_showing() && actions[0] == 0);
	sx_toast("Короткое движение");
	lv_point_t p = body();
	input(p.x, p.y, true);
	input(p.x + 5, p.y + 4, true);
	input(p.x + 5, p.y + 4, false);
	assert(!sx_toast_showing());
	sx_toast("Свайп вниз");
	p = body();
	input(p.x, p.y, true);
	input(p.x, p.y + 35, true);
	input(p.x, p.y + 35, false);
	assert(!sx_toast_showing());
	// Preserve the existing downward threshold, including a diagonal swipe.
	sx_toast("Диагональ вниз");
	p = body();
	input(p.x, p.y, true);
	input(p.x + 100, p.y + 35, true);
	input(p.x + 100, p.y + 35, false);
	assert(!sx_toast_showing());
	sx_toast("Короткий свайп вбок");
	p = body();
	input(p.x, p.y, true);
	input(p.x + 20, p.y + 3, true);
	input(p.x + 20, p.y + 3, false);
	assert(sx_toast_showing());
	sx_toast_dismiss();
}
static void stale_body(void) {
	sx_toast_undo("Первое сообщение", action, (void *)0);
	lv_point_t p = body();
	input(p.x, p.y, true);
	sx_toast_undo("Второе сообщение", action, (void *)1);
	input(p.x, p.y, false);
	assert(sx_toast_showing() && actions[0] == 0 && actions[1] == 0);
	tap(body());
	assert(!sx_toast_showing() && actions[1] == 0);
}
static void lost_replacement(void) {
	sx_toast("Старый жест");
	lv_point_t p = body();
	input(p.x, p.y, true);
	input(5, 5, true);
	sx_toast("Новая цель");
	input(p.x, p.y, true);
	input(p.x, p.y, false);
	assert(sx_toast_showing());
	sx_toast_dismiss();
}
static void stale_undo(void) {
	sx_toast_undo("Первое сообщение", action, (void *)0);
	lv_point_t p = middle(undo_btn);
	input(p.x, p.y, true);
	sx_toast_undo("Второе сообщение", action, (void *)1);
	input(p.x, p.y, false);
	assert(sx_toast_showing() && actions[0] == 0 && actions[1] == 0);
	tap(middle(undo_btn));
	assert(!sx_toast_showing() && actions[1] == 1 && actions[0] == 0);
	// A duplicate CLICKED cannot invoke the consumed callback a second time.
	lv_obj_send_event(undo_btn, LV_EVENT_CLICKED, NULL);
	assert(actions[1] == 1);
	sx_toast_undo("Исходное действие", action, (void *)0);
	tap(middle(undo_btn));
	assert(actions[0] == 1);
}
static void cancel_hold(void) {
	sx_toast_undo("Можно прочитать без спешки", action, (void *)0);
	advance(1000);
	lv_point_t p = body();
	input(p.x, p.y, true);
	advance(8000);
	assert(sx_toast_showing());
	// A drag outside remains press-locked; release resumes its remaining budget.
	input(5, 5, true);
	input(5, 5, false);
	advance(1000);
	assert(sx_toast_showing());
	advance(SX_TOAST_MS);
	assert(!sx_toast_showing());
}
static void missed_release(void) {
	for (int button = 0; button < 2; button++) {
		sx_toast_undo("Чтение после отмены касания", action, (void *)0);
		advance(1000);
		lv_point_t p = button ? middle(undo_btn) : body();
		unsigned before = actions[0];
		input(p.x, p.y, true);
		advance(8000);
		assert(sx_toast_showing());
		lv_indev_wait_release(pointer);
		input(p.x, p.y, false);
		assert(sx_toast_showing() && actions[0] == before);
		advance(1000);
		assert(sx_toast_showing());
		advance(SX_TOAST_MS);
		assert(!sx_toast_showing());
	}
}
static void replacement_budget(void) {
	sx_toast("A");
	advance(2500);
	lv_point_t p = body();
	input(p.x, p.y, true);
	sx_toast("B");
	input(p.x, p.y, false);
	advance(1500);
	assert(sx_toast_showing());
	advance(SX_TOAST_MS);
	assert(!sx_toast_showing());
}
static void disappear(void) {
	for (int i = 0; i < 16; i++) {
		sx_toast_undo("Старое уведомление", action, (void *)0);
		lv_point_t p = middle(undo_btn);
		input(p.x, p.y, true);
		sx_toast_dismiss();
		// The finger moves over the underlying transport while still held.
		input(240, height - 50, true);
		input(240, height - 50, false);
		assert(!sx_toast_showing());
	}
	assert(underlay_clicks == 0 && underlay_presses == 0 && underlay_releases == 0);
	sx_toast("Начали на уведомлении");
	lv_point_t edge = body();
	input(edge.x, edge.y, true);
	input(240, height - 50, true);
	input(240, height - 50, false);
	assert(underlay_clicks == 0 && underlay_presses == 0 && underlay_releases == 0);
	sx_toast_dismiss();
	// Force a queued timeout while held, then reuse the object before release.
	sx_toast("Срок");
	lv_point_t p = body();
	input(p.x, p.y, true);
	end_timer_cb(NULL);
	sx_toast_undo("После таймера", action, (void *)1);
	unsigned before = actions[1];
	input(p.x, p.y, false);
	assert(sx_toast_showing() && actions[1] == before);
	sx_toast_dismiss();
}
static void busy_cases(void) {
	sx_toast_busy("Блокировка до замены", false);
	input(240, height - 50, true);
	sx_toast("Теперь готово");
	input(241, height - 51, true);
	input(241, height - 51, false);
	assert(underlay_clicks == 0 && underlay_presses == 0 && underlay_releases == 0);
	sx_toast_dismiss();

	sx_toast_busy("Защищённая операция", false);
	tap(body());
	assert(sx_toast_busy_showing());
	tap((lv_point_t){240, height - 50});
	assert(underlay_clicks == 0 && underlay_presses == 0 && underlay_releases == 0);
	sx_toast_busy_end();
	sx_toast_busy("Операция продолжается", true);
	tap(body());
	assert(!sx_toast_showing());
	sx_toast_busy("Старое состояние", false);
	lv_point_t p = body();
	input(p.x, p.y, true);
	sx_toast_undo("Готово", action, (void *)1);
	unsigned before = actions[1];
	input(p.x, p.y, false);
	assert(sx_toast_showing() && actions[1] == before);
	sx_toast_dismiss();
	sx_toast("Начало операции");
	p = body();
	input(p.x, p.y, true);
	sx_toast_busy("Операция в фоне", true);
	input(p.x, p.y, false);
	assert(sx_toast_busy_showing());
	advance(SX_TOAST_MS + 100);
	assert(sx_toast_busy_showing());
	tap(body());
	assert(!sx_toast_showing());
}
int main(int argc, char **argv) {
	assert(argc >= 4);
	height = atoi(argv[1]);
	output = argv[3];
	lv_init();
	const char *font_dir = getenv("SONIX_TEST_FONT_DIR");
	assert(font_dir);
	char font_path[1024];
	snprintf(font_path, sizeof(font_path), "%s/ui-regular.ttf", font_dir);
	const lv_font_t *regular = lv_freetype_font_create(font_path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 22, LV_FREETYPE_FONT_STYLE_NORMAL);
	snprintf(font_path, sizeof(font_path), "%s/ui-semibold.ttf", font_dir);
	const lv_font_t *bold = lv_freetype_font_create(font_path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 22, LV_FREETYPE_FONT_STYLE_NORMAL);
	assert(regular && bold);
	font_ui_22 = *regular;
	font_ui_22_bold = *bold;
	font_ui_14 = *regular;
	lv_display_t *d = lv_display_create(480, height);
	lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
	frame = calloc(480 * height, sizeof(*frame));
	assert(frame);
	lv_display_set_buffers(d, frame, NULL, 480 * height * 2, LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(d, flush);
	sx_styles_init();
	lv_style_init(&theme_style_icon);
	bool dark = strcmp(argv[2], "dark") == 0;
	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(dark ? 0x101519 : 0xf4f4f1), 0);
	lv_obj_t *mini = lv_obj_create(screen);
	lv_obj_set_size(mini, 480, SX_MINI_SLOT_H);
	lv_obj_align(mini, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_bg_color(mini, lv_color_hex(0xd8dad5), 0);
	lv_obj_add_event_cb(mini, underlying, LV_EVENT_ALL, NULL);
	lv_obj_t *name = lv_label_create(mini);
	lv_obj_set_style_text_font(name, &font_ui_22, 0);
	lv_label_set_text(name, "Мини-плеер · проверка касания");
	lv_obj_center(name);
	pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_pointer);
	lv_timer_pause(lv_indev_get_read_timer(pointer));
	if (argc > 4 && strcmp(argv[4], "stale-body") == 0) {
		stale_body();
		return 0;
	}
	if (argc > 4 && strcmp(argv[4], "stale-undo") == 0) {
		stale_undo();
		return 0;
	}
	sx_toast_init();
	assert(lv_obj_is_press_lock(toast) && lv_obj_is_press_lock(undo_btn));
	normal();
	lost_replacement();
	stale_body();
	stale_undo();
	cancel_hold();
	missed_release();
	replacement_budget();
	disappear();
	busy_cases();
	assert(underlay_clicks == 0 && underlay_presses == 0 && underlay_releases == 0);
	sx_toast_undo("Ярлык «Аудиокниги» убран с главного экрана", action, (void *)0);
	capture("undo");
	sx_toast("Скачивание прервано: проверьте подключение к сети и повторите попытку позже.");
	capture("long-russian");
	sx_toast_busy("Проверяем сохранение данных перед завершением операции", false);
	capture("busy");
	sx_toast_busy_end();
	tap((lv_point_t){240, height - 50});
	assert(underlay_clicks == 1 && underlay_presses == 1 && underlay_releases == 1);
	printf("PASS toast generation real LVGL pointer %dx%d %s\n", 480, height, argv[2]);
	return 0;
}
