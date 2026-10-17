/*
 * Exercise the real non-HOST update branch with every privileged operation
 * replaced. No device node, shell command, sleep, sync or reboot is performed.
 * From sonix-player/, with LVGL and ALSA headers available:
 * cc -std=gnu11 -O1 -g -Wall -Wextra -DLV_CONF_INCLUDE_SIMPLE -I. \
 *   -ffunction-sections -fdata-sections tests/firmware_update_test.c \
 *   -Wl,--gc-sections -o /tmp/firmware_update_test
 * /tmp/firmware_update_test
 */
#ifdef HOST_BUILD
#error This fixture must exercise the non-HOST production branch
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

static int fixture_system(const char *command);
static int fixture_open(const char *path, int flags, ...);
static ssize_t fixture_read(int fd, void *out, size_t size);
static int fixture_close(int fd);
static void fixture_sync(void);
static unsigned fixture_sleep(unsigned seconds);
static int fixture_reboot(int command);
static bool fixture_recovery_busy;
bool recovery_prep_active(void) { return fixture_recovery_busy; }

#define system fixture_system
#define open fixture_open
#define read fixture_read
#define close fixture_close
#define sync fixture_sync
#define sleep fixture_sleep
#define reboot fixture_reboot
#include "src/system/device/firmware.c"
#undef system
#undef open
#undef read
#undef close
#undef sync
#undef sleep
#undef reboot

enum { SELECTOR_FD = 42 };
typedef struct {
	int command_result;
	bool bad_contents;
	bool open_fails;
	bool close_fails;
	bool interrupted_read;
	size_t readable;
} selector_plan_t;
static selector_plan_t plan[2];
static bool preparation_ok, screen_off, screen_on, remembered, audio_stopped;
static bool shell_reboot_returns;
static int prepared, cancelled, commands, opens, closes, shell_reboots, syscall_reboots;
static int syncs, waits, cache_clears, clock_flushes;
static int screen_holds, auto_off_disables;
static int selector;
static size_t position;
static jmp_buf restarted;
void power_screen_off(void) { screen_off = true; }
void power_screen_on(void) { screen_on = true; }
void power_hold_screen_on(bool hold) {
	assert(hold);
	screen_holds++;
}
void power_set_auto_off(bool enabled, uint32_t minutes) {
	assert(!enabled && minutes == 0);
	auto_off_disables++;
}
void device_state_remember_flush(void) {
	assert(!audio_stopped);
	remembered = true;
}
void audio_stop(void) {
	assert(remembered);
	audio_stopped = true;
}
void stream_provider_clear_on_exit_all(void) { cache_clears++; }
void podcastcache_clear_on_exit(void) { cache_clears++; }
void dlna_clear_on_exit(void) { cache_clears++; }
void clock_shutdown(void) { clock_flushes++; }
bool storage_prepare_for_update(void) {
	assert(audio_stopped && cache_clears == 3 && clock_flushes == 1);
	prepared++;
	return preparation_ok;
}
void storage_cancel_update(void) {
	assert(preparation_ok && commands == 2 && closes >= 1);
	cancelled++;
}

static int fixture_system(const char *command) {
	if (strcmp(command, "reboot") == 0) {
		assert(preparation_ok && commands == 1 && closes == 1);
		shell_reboots++;
		if (!shell_reboot_returns)
			longjmp(restarted, 1);
		return 1;
	}
	assert(preparation_ok && prepared == 1 && commands < 2);
	assert(strcmp(command, commands == 0 ? "/usr/bin/bootmode.sh Recovery" : "/usr/bin/bootmode.sh Normal") == 0);
	selector = commands++;
	position = 0;
	return plan[selector].command_result;
}
static int fixture_open(const char *path, int flags, ...) {
	assert(strcmp(path, "/dev/mtd5") == 0 && flags == (O_RDONLY | O_CLOEXEC));
	opens++;
	if (plan[selector].open_fails) {
		errno = EIO;
		return -1;
	}
	return SELECTOR_FD;
}
static ssize_t fixture_read(int fd, void *out, size_t size) {
	assert(fd == SELECTOR_FD && position <= plan[selector].readable);
	if (plan[selector].interrupted_read) {
		plan[selector].interrupted_read = false;
		errno = EINTR;
		return -1;
	}
	char data[256];
	memset(data, ' ', sizeof(data));
	const char *text = selector ? "ota:kernel" : "ota:kernel2";
	memcpy(data, text, strlen(text));
	if (plan[selector].bad_contents)
		data[10] = 'X';
	if (size > 37)
		size = 37; // Real readback must tolerate short reads.
	if (size > plan[selector].readable - position)
		size = plan[selector].readable - position;
	memcpy(out, data + position, size);
	position += size;
	return (ssize_t)size;
}
static int fixture_close(int fd) {
	assert(fd == SELECTOR_FD);
	closes++;
	if (plan[selector].close_fails) {
		errno = EIO;
		return -1;
	}
	return 0;
}
static void fixture_sync(void) { syncs++; }
static unsigned fixture_sleep(unsigned seconds) {
	assert(seconds == 1 || seconds == 10);
	waits++;
	return 0;
}
static int fixture_reboot(int command) {
	assert(command == RB_AUTOBOOT && shell_reboots == 1);
	syscall_reboots++;
	longjmp(restarted, 2);
}

static void reset(void) {
	memset(plan, 0, sizeof(plan));
	plan[0].readable = plan[1].readable = 256;
	preparation_ok = true;
	screen_off = screen_on = remembered = audio_stopped = shell_reboot_returns = false;
	prepared = cancelled = commands = opens = closes = shell_reboots = syscall_reboots = 0;
	syncs = waits = cache_clears = clock_flushes = 0;
	screen_holds = auto_off_disables = 0;

}
static void expect_refusal(firmware_update_result_t expected) {
	assert(firmware_update_start() == expected);
	assert(prepared == 1 && screen_off && screen_on && remembered && audio_stopped);
	assert(shell_reboots == 0 && syscall_reboots == 0 && syncs == 0 && waits == 0);
	int uncertain = expected == FIRMWARE_UPDATE_BOOTMODE_UNCERTAIN;
	assert(screen_holds == uncertain && auto_off_disables == uncertain);
}

int main(void) {
	reset();
	fixture_recovery_busy = true;
	assert(firmware_update_start() == FIRMWARE_UPDATE_PREPARE_FAILED);
	assert(!screen_off && !screen_on && !remembered && !audio_stopped);
	assert(prepared == 0 && commands == 0 && opens == 0);
	assert(shell_reboots == 0 && syscall_reboots == 0);
	fixture_recovery_busy = false;
	puts("PASS: active recovery preparation refuses update without side effects");
	reset();
	preparation_ok = false;
	expect_refusal(FIRMWARE_UPDATE_PREPARE_FAILED);
	assert(commands == 0 && opens == 0 && cancelled == 0);
	puts("PASS: failed card preparation never touches selector or reboots");

	reset();
	plan[0].command_result = 1;
	expect_refusal(FIRMWARE_UPDATE_PREPARE_FAILED);
	assert(commands == 2 && opens == 1 && cancelled == 1);
	puts("PASS: failed selector command requires verified normal restoration");

	for (int failure = 0; failure < 4; failure++) {
		reset();
		plan[0].bad_contents = failure == 0;
		plan[0].readable = failure == 1 ? 255 : 256;
		plan[0].open_fails = failure == 2;
		plan[0].close_fails = failure == 3;
		expect_refusal(FIRMWARE_UPDATE_PREPARE_FAILED);
		assert(commands == 2 && cancelled == 1);
	}
	puts("PASS: bad/short/unreadable/failed-close selector readback refuses update");

	for (int failure = 0; failure < 2; failure++) {
		reset();
		plan[0].bad_contents = true;
		plan[1].command_result = failure == 0 ? 1 : 0;
		plan[1].bad_contents = failure == 1;
		expect_refusal(FIRMWARE_UPDATE_BOOTMODE_UNCERTAIN);
		assert(commands == 2 && cancelled == 0);
	}
	puts("PASS: uncertain normal selector keeps storage claim and never auto-reboots");

	for (int fallback = 0; fallback < 2; fallback++) {
		reset();
		shell_reboot_returns = fallback;
		plan[0].interrupted_read = true;
		int jump = setjmp(restarted);
		if (!jump) {
			(void)firmware_update_start();
			assert(!"successful device update must not return");
		}
		assert(jump == fallback + 1 && commands == 1 && opens == 1 && closes == 1);
		assert(shell_reboots == 1 && syscall_reboots == fallback && cancelled == 0);
		assert(!screen_on && syncs == 1 && waits == fallback + 1);
		assert(screen_holds == 0 && auto_off_disables == 0);
	}
	puts("PASS: confirmed selector reaches intercepted reboot without reading or patching recovery, including fallback");
	return 0;
}
