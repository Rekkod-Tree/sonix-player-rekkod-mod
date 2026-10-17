// cc -std=gnu11 -O1 -Wall -DLV_CONF_INCLUDE_SIMPLE -I. -I/path/to/checkout tests/lowbattery_test.c -o /tmp/lowbattery_test
// All timers, readings, notices and power operations below are fixture stubs.
#include "src/gui/shell/lowbattery.c"
#include <assert.h>
#include <string.h>
static char reading[24] = "90";
static int millivolts = 4200, shutdowns, warnings;
static bool cable, refuse_timer;
const lv_image_dsc_t icon_battery_warning = {0};
lv_color_t lv_color_make(uint8_t red, uint8_t green, uint8_t blue) { return (lv_color_t){.red = red, .green = green, .blue = blue}; }

void sync_battery_from_sysfs(void) {}
char *read_battery_percent(void) { return reading; }
int read_battery_millivolts(void) { return millivolts; }
bool usb_vbus_present(void) { return cable; }
bool read_battery_charging(void) { return false; }
bool power_screen_is_on(void) { return true; }
void power_screen_on(void) {}
void power_shutdown(void) { shutdowns++; }
void confirm_close(void) {}
void confirm_notice(const char *title, const char *message) {
	(void)title;
	(void)message;
	warnings++;
}
const char *tr(const char *key) { return key; }
void gui_modal_show(const lv_image_dsc_t *icon, lv_color_t color, const char *title, const char *subtitle) {
	(void)icon;
	(void)color;
	(void)title;
	(void)subtitle;
}
void gui_modal_hide(void) {}
lv_timer_t *lv_timer_create(lv_timer_cb_t callback, uint32_t period, void *user) {
	(void)callback;
	(void)period;
	(void)user;
	return refuse_timer ? NULL : (lv_timer_t *)(uintptr_t)1;
}
void lv_timer_delete(lv_timer_t *timer) { (void)timer; }

static void reset_low(const char *percent, int mv) {
	countdown_stop();
	low_readings = 0;
	cable = false;
	snprintf(reading, sizeof(reading), "%s", percent);
	millivolts = mv;
}
static void start_low(void) {
	reset_low("1", 3500);
	check_cb(NULL);
	check_cb(NULL);
	assert(!countdown_timer);
	check_cb(NULL);
	assert(countdown_timer);
}
int main(void) {
	reset_low("1", 3500);
	refuse_timer = true;
	check_cb(NULL);
	check_cb(NULL);
	check_cb(NULL);
	assert(!countdown_timer && !shutdowns);
	refuse_timer = false;
	const char *invalid[] = {"!!", "101", "-1", "1%", ""};
	for (int i = 0; i < 5; i++) {
		reset_low(invalid[i], 3500);
		for (int n = 0; n < 5; n++)
			check_cb(NULL);
		assert(!countdown_timer && !shutdowns);
	}
	int unsafe[] = {-1, 0, 1000, 3600, 4200};
	for (int i = 0; i < 5; i++) {
		reset_low("0", unsafe[i]);
		for (int n = 0; n < 5; n++)
			check_cb(NULL);
		assert(!countdown_timer && !shutdowns);
	}
	start_low();
	strcpy(reading, "2");
	countdown_cb(NULL);
	assert(!countdown_timer && !shutdowns);
	start_low();
	millivolts = -1;
	countdown_cb(NULL);
	assert(!countdown_timer && !shutdowns);
	start_low();
	cable = true;
	countdown_cb(NULL);
	assert(!countdown_timer && !shutdowns);
	start_low();
	for (int i = 0; i < 10; i++)
		countdown_cb(NULL);
	assert(shutdowns == 1 && !countdown_timer);
	reset_low("5", 3700);
	warned = false;
	check_cb(NULL);
	check_cb(NULL);
	assert(warnings == 1);
	strcpy(reading, "10");
	check_cb(NULL);
	strcpy(reading, "4");
	check_cb(NULL);
	assert(warnings == 2);
	puts("PASS battery guard: strict readings, unknown/high voltage, fresh cancellation, charger and confirmed low shutdown");
}
