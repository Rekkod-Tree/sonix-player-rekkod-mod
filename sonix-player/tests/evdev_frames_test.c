#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
/* Entire production dependency driver, not a modeled event reader. */
#include EVDEV_IMPLEMENTATION
#if !LV_USE_EVDEV || LV_USE_GESTURE_RECOGNITION
#error This fixture expects native enabled evdev with disabled optional gesture recognizers.
#endif
/* Inert ownership/display API seams. No timer, UI, device node, or LVGL worker is started. */
static char indev_tag;
static lv_display_t display;
static lv_evdev_t driver;
static lv_indev_t *const pointer = (lv_indev_t *)&indev_tag;
lv_indev_type_t lv_indev_get_type(const lv_indev_t *i) {
	assert(i == pointer);
	return LV_INDEV_TYPE_POINTER;
}
lv_display_t *lv_indev_get_display(const lv_indev_t *i) {
	assert(i == pointer);
	return &display;
}
void *lv_indev_get_driver_data(const lv_indev_t *i) {
	assert(i == pointer);
	return &driver;
}
void lv_indev_delete(lv_indev_t *i) {
	(void)i;
	assert(!"Unexpected device deletion");
}
lv_result_t lv_async_call(lv_async_cb_t cb, void *u) {
	(void)cb;
	(void)u;
	assert(!"Unexpected async driver error");
	return LV_RESULT_INVALID;
}
void lv_log_add(lv_log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
	(void)file;
	(void)line;
	(void)func;
	if (level > LV_LOG_LEVEL_ERROR)
		return;
	va_list a;
	va_start(a, fmt);
	vfprintf(stderr, fmt, a);
	va_end(a);
	fputc('\n', stderr);
	abort();
}
static int writer;
static void fresh(void) {
	int p[2];
	assert(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0);
	memset(&driver, 0, sizeof(driver));
	driver.fd = p[0];
	driver.min_x = 0;
	driver.min_y = 0;
	driver.max_x = 479;
	driver.max_y = 799;
	driver.state = LV_INDEV_STATE_RELEASED;
	writer = p[1];
}
static void finish(void) {
	assert(close(writer) == 0);
	assert(close(driver.fd) == 0);
}
static void packet(unsigned short type, unsigned short code, int value) {
	struct input_event e = {.type = type, .code = code, .value = value};
	assert(gettimeofday(&e.time, NULL) == 0);
	assert(write(writer, &e, sizeof(e)) == (ssize_t)sizeof(e));
}
static void sync_packet(void) {
	packet(EV_SYN, SYN_MT_REPORT, 0);
	packet(EV_SYN, SYN_REPORT, 0);
}
static void press(int x, int y) {
	packet(EV_ABS, ABS_MT_TRACKING_ID, 41);
	packet(EV_ABS, ABS_MT_POSITION_X, x);
	packet(EV_ABS, ABS_MT_POSITION_Y, y);
	packet(EV_ABS, ABS_MT_TOUCH_MAJOR, 8);
	packet(EV_ABS, ABS_MT_PRESSURE, 32);
	packet(EV_KEY, BTN_TOUCH, 1);
	sync_packet();
}
static void move(int x, int y) {
	packet(EV_ABS, ABS_MT_POSITION_X, x);
	packet(EV_ABS, ABS_MT_POSITION_Y, y);
	sync_packet();
}
static void release(void) {
	packet(EV_ABS, ABS_MT_TRACKING_ID, -1);
	packet(EV_KEY, BTN_TOUCH, 0);
	sync_packet();
}

static lv_indev_data_t sample(lv_indev_state_t state, int x, int y, bool more) {
	lv_indev_data_t d = {0};
	_evdev_read(pointer, &d);
	assert(!driver.deleting);
	assert(d.state == state);
	assert(d.point.x == x && d.point.y == y);
	assert(d.continue_reading == more);
	return d;
}
static void empty(lv_indev_state_t state, int x, int y) {
	sample(state, x, y, false);
	int queued = -1;
	assert(ioctl(driver.fd, FIONREAD, &queued) == 0 && queued == 0);
}
int main(void) {
	alarm(5);
	display.hor_res = 480;
	display.ver_res = 800;
	// Two complete taps queued before the first GUI read: no merged or duplicate edges.
	fresh();
	press(240, 735);
	release();
	press(100, 200);
	release();
	sample(LV_INDEV_STATE_PRESSED, 240, 735, true);
	sample(LV_INDEV_STATE_RELEASED, 240, 735, true);
	sample(LV_INDEV_STATE_PRESSED, 100, 200, true);
	sample(LV_INDEV_STATE_RELEASED, 100, 200, true);
	empty(LV_INDEV_STATE_RELEASED, 100, 200);
	empty(LV_INDEV_STATE_RELEASED, 100, 200);
	finish();
	// A queued drag preserves its intermediate coordinates and final release.
	fresh();
	press(10, 20);
	move(80, 40);
	move(160, 60);
	release();
	sample(LV_INDEV_STATE_PRESSED, 10, 20, true);
	sample(LV_INDEV_STATE_PRESSED, 80, 40, true);
	sample(LV_INDEV_STATE_PRESSED, 160, 60, true);
	sample(LV_INDEV_STATE_RELEASED, 160, 60, true);
	empty(LV_INDEV_STATE_RELEASED, 160, 60);
	finish();
	// A held finger, including an empty read, remains pressed until release.
	fresh();
	press(240, 400);
	sample(LV_INDEV_STATE_PRESSED, 240, 400, true);
	empty(LV_INDEV_STATE_PRESSED, 240, 400);
	release();
	sample(LV_INDEV_STATE_RELEASED, 240, 400, true);
	empty(LV_INDEV_STATE_RELEASED, 240, 400);
	finish();
	// Protocol A: BTN_TOUCH-only release; SYN_MT_REPORT must not end the frame.
	fresh();
	packet(EV_ABS, ABS_MT_POSITION_X, 44);
	packet(EV_ABS, ABS_MT_POSITION_Y, 55);
	packet(EV_KEY, BTN_TOUCH, 1);
	sync_packet();
	packet(EV_KEY, BTN_TOUCH, 0);
	sync_packet();
	sample(LV_INDEV_STATE_PRESSED, 44, 55, true);
	sample(LV_INDEV_STATE_RELEASED, 44, 55, true);
	empty(LV_INDEV_STATE_RELEASED, 44, 55);
	finish();
	// Rotation/calibration stays in the existing driver, not in the frame patch.
	fresh();
	driver.min_x = 479;
	driver.max_x = 0;
	driver.min_y = 799;
	driver.max_y = 0;
	press(100, 200);
	release();
	sample(LV_INDEV_STATE_PRESSED, 379, 599, true);
	sample(LV_INDEV_STATE_RELEASED, 379, 599, true);
	empty(LV_INDEV_STATE_RELEASED, 379, 599);
	finish();
	puts("PASS actual evdev: queued taps, no duplicate/idle reads, queued drag, held finger, Protocol A/B, rotation");
	return 0;
}
