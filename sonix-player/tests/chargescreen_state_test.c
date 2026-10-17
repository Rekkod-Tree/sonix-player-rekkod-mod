// Non-HOST charging-state fixture: device nodes, PMIC, sysfs, framebuffer,
// input, clock and process operations are replaced below. No devices are used.
// From sonix-player: cc -std=gnu11 -I. $(pkg-config --cflags freetype2)
// tests/chargescreen_state_test.c src/system/image/png_write.c
// $(pkg-config --libs freetype2) -lm -ldl -pthread -o /tmp/charge_test
// Run without arguments for state checks; add an output folder and font file
// to render previews without touching device nodes.
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>
#include FT_FREETYPE_H
#include "src/system/image/png_write.h"
#ifdef HOST_BUILD
#error Exercise the production charging branch with intercepted operations
#endif

// The charge-only code uses no LVGL facilities from power.h.
#define POWER_H
void power_set_charge_limit(int percent);
static DIR *fake_opendir(const char *path);
static struct dirent *fake_readdir(DIR *dir);
static int fake_closedir(DIR *dir);
static FILE *fake_fopen(const char *path, const char *mode);
static int fake_open(const char *path, int flags, ...);
static int fake_close(int fd);
static int fake_ioctl(int fd, unsigned long request, ...);
static void *fake_mmap(void *start, size_t size, int prot, int flags, int fd, off_t offset);
static int fake_munmap(void *address, size_t size);
static int fake_access(const char *path, int mode);
static int fake_system(const char *command);
static int fake_reboot(int command);
static void fake_sync(void);
static unsigned fake_sleep(unsigned seconds);
static int fake_usleep(useconds_t micros);
static int fake_poll(struct pollfd *fds, nfds_t count, int timeout);
static ssize_t fake_read(int fd, void *out, size_t size);
static int fake_clock_gettime(clockid_t id, struct timespec *out);
static FT_Error fake_ft_init(FT_Library *library);
#define opendir fake_opendir
#define readdir fake_readdir
#define closedir fake_closedir
#define fopen fake_fopen
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#define access fake_access
#define system fake_system
#define reboot fake_reboot
#define sync fake_sync
#define sleep fake_sleep
#define usleep fake_usleep
#define poll fake_poll
#define read fake_read
#define clock_gettime fake_clock_gettime
#define FT_Init_FreeType fake_ft_init
#include "src/system/device/chargescreen.c"
#undef opendir
#undef readdir
#undef closedir
#undef fopen
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef access
#undef system
#undef reboot
#undef sync
#undef sleep
#undef usleep
#undef poll
#undef read
#undef clock_gettime
#undef FT_Init_FreeType

typedef struct {
	const char *name, *type, *online, *capacity;
} supply_t;
static supply_t supplies[5];
static int supply_count, directory_index;
static bool pmic_model, pmic_known, pmic_vbus, directory_missing, mark_known, source_known;
static bool input_missing, input_down, unknown_after_first;
static uint8_t mark_value, source_value;
static int pmic_reads, mark_writes, fd_opens, fd_closes, mappings, unmaps, framebuffer_failure;
static int font_starts, charge_settings, fake_writes, states_checked;
static int dir_token;
static char supply_path[320];

bool axpcharge_applies(void) { return pmic_model; }
bool axpcharge_reg_read(uint8_t reg, uint8_t *out) {
	if (reg == AXP_REG_STATUS1) {
		pmic_reads++;
		if (!pmic_known || (unknown_after_first && pmic_reads > 1))
			return false;
		*out = pmic_vbus ? AXP_STATUS1_VBUS_GOOD : 0;
		return true;
	}
	if (reg == AXP_REG_MARK) {
		*out = mark_value;
		return mark_known;
	}
	if (reg == AXP_REG_PWRON_STATUS) {
		*out = source_value;
		return source_known;
	}
	assert(reg >= AXP_REG_IRQ_STATUS0 && reg < AXP_REG_IRQ_STATUS0 + 3);
	*out = 0;
	return true;
}
bool axpcharge_reg_write(uint8_t reg, uint8_t value) {
	assert(reg == AXP_REG_MARK);
	mark_writes++;
	mark_value = value;
	return true;
}
void axpcharge_set(bool voltage, bool current) {
	(void)voltage;
	(void)current;
	charge_settings++;
}
void axpcharge_tick(void) { assert(!"fixture must not enter persistent charging loop"); }
void power_set_charge_limit(int percent) {
	assert(percent == 100);
	charge_settings++;
}
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	(void)key;
	return fallback;
}
bool config_get_bool(const char *section, const char *key, bool fallback) {
	(void)section;
	(void)key;
	return fallback;
}

static DIR *fake_opendir(const char *path) {
	if (!strcmp(path, "/dev/input"))
		return NULL;
	assert(!strcmp(path, SUPPLY_DIR));
	directory_index = 0;
	return directory_missing ? NULL : (DIR *)&dir_token;
}
static struct dirent *fake_readdir(DIR *dir) {
	assert(dir == (DIR *)&dir_token);
	if (directory_index == supply_count)
		return NULL;
	static struct dirent entry;
	memset(&entry, 0, sizeof(entry));
	snprintf(entry.d_name, sizeof(entry.d_name), "%s", supplies[directory_index++].name);
	return &entry;
}
static int fake_closedir(DIR *dir) {
	assert(dir == (DIR *)&dir_token);
	return 0;
}
static FILE *fake_fopen(const char *path, const char *mode) {
	if (!strcmp(mode, "w")) {
		fake_writes++;
		return NULL;
	}
	assert(!strcmp(mode, "r"));
	const char *value = NULL;
	if (!strcmp(path, BACKLIGHT_MAX_NODE))
		value = "100\n";
	for (int i = 0; i < supply_count; i++) {
		const char *fields[] = {"type", "online", "capacity"};
		const char *values[] = {supplies[i].type, supplies[i].online, supplies[i].capacity};
		for (int f = 0; f < 3; f++) {
			snprintf(supply_path, sizeof(supply_path), SUPPLY_DIR "/%s/%s", supplies[i].name, fields[f]);
			if (!strcmp(path, supply_path))
				value = values[f];
		}
	}
	return value ? fmemopen((void *)value, strlen(value), "r") : NULL;
}
static int fake_open(const char *path, int flags, ...) {
	(void)flags;
	if (!strcmp(path, FB_DEV)) {
		if (framebuffer_failure == 1)
			return -1;
		fd_opens++;
		return 42;
	}
	assert(!strcmp(path, KEYS_FALLBACK));
	if (input_missing)
		return -1;
	fd_opens++;
	return 43;
}
static int fake_close(int fd) {
	assert(fd == 42 || fd == 43);
	fd_closes++;
	return 0;
}
static int fake_ioctl(int fd, unsigned long request, ...) {
	va_list ap;
	va_start(ap, request);
	void *out = va_arg(ap, void *);
	va_end(ap);
	if (fd == 43) {
		assert(_IOC_NR(request) == _IOC_NR(EVIOCGKEY(1)));
		memset(out, 0, _IOC_SIZE(request));
		if (input_down)
			((unsigned long *)out)[KEY_POWER / (8 * sizeof(unsigned long))] |= 1ul << (KEY_POWER % (8 * sizeof(unsigned long)));
		return 0;
	}
	assert(fd == 42);
	if (request == FBIOGET_VSCREENINFO) {
		if (framebuffer_failure == 2)
			return -1;
		struct fb_var_screeninfo *var = out;
		memset(var, 0, sizeof(*var));
		var->xres = 480;
		var->yres = 800;
		var->xres_virtual = 480;
		var->yres_virtual = 1600;
		var->bits_per_pixel = framebuffer_failure == 3 ? 8 : 16;
		return 0;
	}
	if (request == FBIOPUT_VSCREENINFO)
		return 0;
	assert(request == FBIOGET_FSCREENINFO);
	if (framebuffer_failure == 4)
		return -1;
	struct fb_fix_screeninfo *fix = out;
	memset(fix, 0, sizeof(*fix));
	fix->line_length = 960;
	fix->smem_len = framebuffer_failure == 5 ? 1 : 960 * 1600;
	return 0;
}
static void *fake_mmap(void *start, size_t size, int prot, int flags, int fd, off_t offset) {
	(void)start;
	(void)prot;
	(void)flags;
	assert(fd == 42 && offset == 0 && size <= 960u * 1600u);
	if (framebuffer_failure == 6)
		return MAP_FAILED;
	void *memory = calloc(1, size);
	assert(memory);
	mappings++;
	return memory;
}
static int fake_munmap(void *address, size_t size) {
	assert(address && size);
	free(address);
	unmaps++;
	return 0;
}
static int fake_access(const char *path, int mode) {
	(void)path;
	(void)mode;
	assert(!"font init is disabled");
	return -1;
}
static int fake_system(const char *command) {
	(void)command;
	assert(!"no external commands allowed");
	return -1;
}
static int fake_reboot(int command) {
	(void)command;
	assert(!"no reboot allowed");
	return -1;
}
static void fake_sync(void) { assert(!"no sync allowed"); }
static unsigned fake_sleep(unsigned seconds) {
	(void)seconds;
	assert(!"no sleep allowed");
	return 0;
}
static int fake_usleep(useconds_t micros) {
	(void)micros;
	assert(!"no loop allowed");
	return 0;
}
static int fake_poll(struct pollfd *fds, nfds_t count, int timeout) {
	(void)fds;
	(void)count;
	(void)timeout;
	assert(!"no loop allowed");
	return -1;
}
static ssize_t fake_read(int fd, void *out, size_t size) {
	(void)fd;
	(void)out;
	(void)size;
	assert(!"no input reads allowed");
	return -1;
}
static int fake_clock_gettime(clockid_t id, struct timespec *out) {
	(void)id;
	(void)out;
	assert(!"no clock access allowed");
	return -1;
}
static FT_Error fake_ft_init(FT_Library *library) {
	*library = NULL;
	font_starts++;
	return 1;
}

static void reset(void) {
	assert(fb_fd == -1 && !fb_mem);
	memset(supplies, 0, sizeof(supplies));
	supply_count = 0;
	directory_index = 0;
	pmic_model = pmic_known = mark_known = source_known = true;
	pmic_vbus = directory_missing = input_missing = input_down = unknown_after_first = false;
	mark_value = CHARGESCREEN_MARK_NONE;
	source_value = 0;
	pmic_reads = mark_writes = fd_opens = fd_closes = mappings = unmaps = framebuffer_failure = 0;
	font_starts = charge_settings = fake_writes = 0;
}
static void expect_state(int expected) {
	assert(cable_state() == expected);
	states_checked++;
}
static void cable_cases(void) {
	reset();
	expect_state(0);
	pmic_vbus = true;
	expect_state(1);
	pmic_known = false;
	expect_state(-1);
	assert(cable_in());
	directory_missing = true;
	expect_state(-1);
	reset();
	pmic_model = false;
	expect_state(-1);
	supplies[supply_count++] = (supply_t){"unused", "USB", "0", NULL};
	expect_state(-1);
	supplies[supply_count++] = (supply_t){"battery", "Battery", "1", "50"};
	expect_state(-1);
	supplies[supply_count++] = (supply_t){"mp2731-charger", "USB", NULL, NULL};
	expect_state(-1);
	supplies[2].online = "2";
	expect_state(-1);
	supplies[2].online = "0";
	expect_state(0);
	supplies[2].online = "1";
	expect_state(1);
	supplies[2].online = NULL;
	pmic_vbus = true;
	expect_state(1);
	pmic_vbus = false;
	directory_missing = true;
	expect_state(-1);
	reset();
	pmic_known = false;
	supplies[supply_count++] = (supply_t){"charger", "Mains", "1", NULL};
	expect_state(1);
	printf("PASS %d R1/R3 known/unknown cable cases, including irrelevant supply and PMIC-positive override\n", states_checked);
}
static void wanted_cases(void) {
	reset();
	pmic_known = false;
	assert(!wanted() && !mark_writes);
	reset();
	pmic_vbus = true;
	unknown_after_first = true;
	assert(wanted() && pmic_reads == 1);
	reset();
	pmic_vbus = true;
	source_value = AXP_PWRON_KEY;
	assert(!wanted());
	reset();
	pmic_vbus = true;
	mark_value = CHARGESCREEN_MARK_STARTED;
	assert(!wanted());
	reset();
	pmic_vbus = true;
	mark_value = CHARGESCREEN_MARK_REQUESTED;
	assert(wanted() && !mark_writes);
	reset();
	mark_value = CHARGESCREEN_MARK_REQUESTED;
	assert(!wanted() && mark_writes == 1 && mark_value == CHARGESCREEN_MARK_NONE);
	reset();
	pmic_vbus = true;
	mark_known = false;
	assert(!wanted());
	reset();
	pmic_model = false;
	supplies[supply_count++] = (supply_t){"mp2731-charger", "USB", "1", NULL};
	assert(wanted());
	input_down = true;
	assert(!wanted());
	assert(fd_opens == fd_closes);
	puts("PASS wanted uses one cable snapshot; normal/requested/started/key/unknown paths and R3 held key");
}
static void failure_cases(void) {
	for (int failure = 1; failure <= 6; failure++) {
		reset();
		pmic_vbus = true;
		mark_value = CHARGESCREEN_MARK_REQUESTED;
		framebuffer_failure = failure;
		chargescreen_run_if_wanted();
		assert(fd_opens == fd_closes && !fb_mem && fb_fd == -1);
		assert(!font_starts && !charge_settings && !fake_writes && !mark_writes);
	}
	reset();
	pmic_vbus = true;
	mark_value = CHARGESCREEN_MARK_REQUESTED;
	input_missing = true;
	chargescreen_run_if_wanted();
	assert(fd_opens == fd_closes && mappings == 1 && unmaps == 1);
	assert(fb_fd == -1 && !fb_mem && font_starts == 1 && !mark_writes);
	reset();
	assert(battery_percent(-1) == -1);
	supplies[supply_count++] = (supply_t){"battery", "Battery", NULL, "101"};
	assert(battery_percent(47) == 47);
	supplies[0].capacity = "0";
	assert(battery_percent(-1) == 0);
	supplies[supply_count++] = (supply_t){"good", "Battery", NULL, "64"};
	assert(battery_percent(-1) == 64);
	puts("PASS six framebuffer failure stages and missing input return to normal boot without descriptor/mapping leaks; unknown percent remains unknown");
}
static void render_cases(const char *out, const char *font) {
	assert(FT_Init_FreeType(&ft_lib) == 0);
	assert(FT_New_Face(ft_lib, font, 0, &ft_face) == 0);
	const int levels[] = {-1, 0, 1, 5, 50, 80, 100, 101};
	for (int height = 720; height <= 800; height += 80) {
		for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
			size_t size = (size_t)480 * height * 2;
			uint8_t *allocation = malloc(size + 32);
			assert(allocation);
			memset(allocation, 0xA5, size + 32);
			canvas_t cv = {allocation + 16, 480, height, 960, 16};
			draw_page(&cv, levels[i]);
			for (int guard = 0; guard < 16; guard++)
				assert(allocation[guard] == 0xA5 && allocation[size + 16 + guard] == 0xA5);
			char path[512];
			snprintf(path, sizeof(path), "%s/charge-%d-%d.png", out, height, levels[i]);
			assert(png_write_rgb565(path, (uint16_t *)cv.mem, cv.w, cv.h));
			// Repainting the same frame must be deterministic, not blend stale pixels.
			uint8_t *first = malloc(size);
			assert(first);
			memcpy(first, cv.mem, size);
			draw_page(&cv, levels[i]);
			assert(memcmp(first, cv.mem, size) == 0);
			free(first);
			free(allocation);
		}
	}
	font_close();
	puts("PASS Graphite charging render: both panels, unknown/zero/low/normal/full levels, bounds and repeatability");
}

int main(int argc, char **argv) {
	if (argc == 3) {
		render_cases(argv[1], argv[2]);
		return 0;
	}
	cable_cases();
	wanted_cases();
	failure_cases();
	puts("PASS charging fixture: no physical device, clock, PMIC, sysfs write or command executed");
	return 0;
}
