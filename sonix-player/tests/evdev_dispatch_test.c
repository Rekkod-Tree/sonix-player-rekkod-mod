// Real LVGL dispatch with the production evdev reader and tap guard.
// Enable only the driver under test; the remaining configuration matches host objects.
#define _GNU_SOURCE
#include "lv_conf.h"
#undef LV_USE_EVDEV
#define LV_USE_EVDEV 1
#include EVDEV_IMPLEMENTATION
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>

lv_font_t font_ui_14 = {.line_height = 14}; // No text is created in this scene.
#define TAP_SLOP_PX 12
#include TAP_GUARD_IMPLEMENTATION

static unsigned clicks, presses, releases;
static int writer;
static void event(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) clicks++;
	if (lv_event_get_code(e) == LV_EVENT_PRESSED) presses++;
	if (lv_event_get_code(e) == LV_EVENT_RELEASED) releases++;
}
static void packet(unsigned short type, unsigned short code, int value) {
	struct input_event e = {.type = type, .code = code, .value = value};
	assert(write(writer, &e, sizeof(e)) == sizeof(e));
}
static void frame(bool down, int x, int y) {
	packet(EV_ABS, ABS_MT_TRACKING_ID, down ? 1 : -1);
	packet(EV_ABS, ABS_MT_POSITION_X, x);
	packet(EV_ABS, ABS_MT_POSITION_Y, y);
	packet(EV_KEY, BTN_TOUCH, down);
	packet(EV_SYN, SYN_MT_REPORT, 0);
	packet(EV_SYN, SYN_REPORT, 0);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
	(void)area; (void)pixels;
	lv_display_flush_ready(display);
}
int main(void) {
	alarm(10);
	lv_init();
	lv_display_t *display = lv_display_create(480, 800);
	static uint8_t pixels[480 * 40 * 4];
	lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, flush);
	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_scrollable(screen, false);
	lv_obj_t *button = lv_obj_create(screen);
	lv_obj_set_size(button, 400, 600);
	lv_obj_set_pos(button, 40, 40);
	lv_obj_set_clickable(button, true);
	lv_obj_set_scrollable(button, false);
	lv_obj_add_event_cb(button, event, LV_EVENT_ALL, NULL);
	lv_obj_update_layout(screen);
	int fds[2];
	assert(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
	writer = fds[1];
	lv_evdev_t driver = {.fd = fds[0], .type = LV_EVDEV_TYPE_ABS, .max_x = 479, .max_y = 799};
	lv_indev_t *pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_driver_data(pointer, &driver);
	lv_indev_set_read_cb(pointer, _evdev_read);
	lv_indev_add_event_cb(pointer, tap_guard_cb, LV_EVENT_ALL, NULL);

	frame(true, 150, 150); frame(false, 150, 150);
	lv_indev_read(pointer);
	assert(clicks == 1 && presses == 1 && releases == 1);
	frame(true, 150, 150); frame(false, 150, 150);
	frame(true, 170, 170); frame(false, 170, 170);
	lv_indev_read(pointer);
	assert(clicks == 3 && presses == 3 && releases == 3);
	lv_indev_read(pointer); lv_indev_read(pointer);
	assert(clicks == 3 && releases == 3);
	frame(true, 150, 150); frame(true, 220, 150); frame(false, 220, 150);
	lv_indev_read(pointer);
	assert(clicks == 3 && presses == 4 && releases == 4); // Drag is not a tap.
	frame(true, 150, 150); lv_indev_read(pointer); lv_indev_read(pointer);
	assert(clicks == 3 && presses == 5 && releases == 4);
	lv_indev_wait_release(pointer);
	frame(false, 150, 150); lv_indev_read(pointer);
	assert(clicks == 3); // Cancelled touch never clicks.
	frame(true, 150, 150); frame(false, 150, 150); lv_indev_read(pointer);
	assert(clicks == 4); // Following tap recovers.
	close(writer); close(fds[0]);
	puts("PASS real LVGL + production tap guard/evdev: queued clicks, drag cancellation, held/released, cancel recovery, no duplicate idle click");
	return 0;
}
