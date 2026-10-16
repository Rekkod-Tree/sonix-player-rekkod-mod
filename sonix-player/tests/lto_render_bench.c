// Offscreen renderer benchmark: actual LVGL and production fonts, no player.
// Compile this file with the variant's TARGET_CFLAGS and TARGET_DEFAULT_OPT.
// Link with that variant's build_target/lvgl/**/*.o, gui/fonts/fonts.o,
// system/core/sha256.o and libfreetype.a; use TARGET_LD, -Wl,--gc-sections,
// -static-libstdc++ -static-libgcc -lm -pthread, and its target LTO/link flags.
// No framebuffer/input/audio drivers are created. The only external files
// read are installed fonts. Results go to stdout; no output files are created.
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "src/gui/fonts/fonts.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/core/respath.h"
#include "src/system/core/sha256.h"

#define WIDTH 480
#define HEIGHT 800
#define WARMUP_FRAMES 8
#define MEASURED_FRAMES 120
#define ROWS 18
#define ROW_PITCH 82

// Production fonts_init reads one preference and the current language. This
// fixed workload must not load or save the user's settings or language files.
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	(void)key;
	return fallback;
}
const char *lang_current(void) { return "Russian"; }
const char *tr(const char *key) { return key; }

static uint16_t pixels[WIDTH * HEIGHT];
static unsigned flushes;
static bool bad_flush;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
	if (area->x1 != 0 || area->y1 != 0 || area->x2 != WIDTH - 1 || area->y2 != HEIGHT - 1 || data != (uint8_t *)pixels)
		bad_flush = true;
	flushes++;
	lv_display_flush_ready(display);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
	lv_obj_t *obj = lv_obj_create(parent);
	lv_obj_remove_style_all(obj);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, radius, 0);
	lv_obj_set_scrollable(obj, false);
	return obj;
}

static lv_obj_t *text(lv_obj_t *parent, int x, int y, int width, const char *value, const lv_font_t *font, uint32_t color) {
	lv_obj_t *label = lv_label_create(parent);
	lv_obj_remove_style_all(label);
	lv_obj_set_pos(label, x, y);
	lv_obj_set_size(label, width, lv_font_get_line_height(font));
	lv_obj_set_style_text_font(label, font, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
	lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
	lv_label_set_text(label, value);
	return label;
}

static lv_obj_t *scene(lv_obj_t *screen) {
	lv_obj_remove_style_all(screen);
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x111820), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_scrollable(screen, false);
	text(screen, 22, 24, 430, "Медиатека", &font_ui_34_bold, 0xf2f4f6);
	text(screen, 24, 70, 430, "Избранное · Альбомы · Исполнители", &font_ui_18, 0xa8b3c0);
	lv_obj_t *tile = box(screen, 16, 110, 216, 112, 0x293645, 18);
	box(tile, 16, 14, 28, 28, 0x8eb4d6, 8);
	text(tile, 16, 52, 186, "Музыка", &font_ui_24_bold, 0xf2f4f6);
	text(tile, 16, 84, 186, "18 треков", &font_ui_16, 0xb7c2ce);
	tile = box(screen, 248, 110, 216, 112, 0x323442, 18);
	box(tile, 16, 14, 28, 28, 0xc2a0bf, 8);
	text(tile, 16, 52, 186, "Альбомы", &font_ui_24_bold, 0xf2f4f6);
	text(tile, 16, 84, 186, "Недавно добавлены", &font_ui_16, 0xb7c2ce);

	lv_obj_t *list = box(screen, 16, 240, 448, 462, 0x111820, 14);
	lv_obj_set_scrollable(list, true);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_clip_corner(list, true, 0);
	lv_obj_t *body = box(list, 0, 0, 448, ROWS * ROW_PITCH, 0x111820, 0);
	static const char *const titles[] = {"Time", "Я боюсь", "Echoes", "А теперь поёт медведь", "Северный ветер", "Eclipse"};
	static const char *const artists[] = {"Pink Floyd", "Хаски", "Pink Floyd", "Нейромонах Феофан", "Музыка без границ", "Pink Floyd"};
	static const uint32_t colors[] = {0x7688ac, 0xb47b69, 0x839b83, 0xb3a273, 0x9d7d9e, 0x6f9a9e};
	for (int i = 0; i < ROWS; i++) {
		int style = i % 6;
		lv_obj_t *row = box(body, 0, i * ROW_PITCH, 448, 74, i % 2 ? 0x202b36 : 0x26313d, 14);
		lv_obj_t *cover = box(row, 12, 11, 52, 52, colors[style], 10);
		text(cover, 14, 9, 30, i % 2 ? "М" : "P", &font_ui_24_bold, 0x18212b);
		text(row, 78, 9, 352, titles[style], &font_ui_22_bold, 0xf2f4f6);
		text(row, 78, 43, 286, artists[style], &font_ui_17, 0xb3bfca);
		char duration[16];
		snprintf(duration, sizeof(duration), "%d:%02d", 3 + i % 6, (i * 7) % 60);
		lv_obj_t *length = text(row, 388, 43, 44, duration, &font_ui_16, 0xa4b1be);
		lv_obj_set_style_text_align(length, LV_TEXT_ALIGN_RIGHT, 0);
	}

	lv_obj_t *mini = box(screen, 16, 718, 448, 66, 0xe0d8c8, 18);
	box(mini, 12, 10, 46, 46, 0x78939a, 10);
	text(mini, 72, 7, 300, "The Great Gig in the Sky", &font_ui_22_bold, 0x202c38);
	text(mini, 72, 38, 280, "Pink Floyd · FLAC", &font_ui_16, 0x53616e);
	box(mini, 393, 21, 6, 24, 0x202c38, 2);
	box(mini, 405, 21, 6, 24, 0x202c38, 2);
	return list;
}

static bool timestamp(clockid_t clock, struct timespec *value) { return clock_gettime(clock, value) == 0; }
static double elapsed_ms(struct timespec start, struct timespec end) { return (double)(end.tv_sec - start.tv_sec) * 1000.0 + (double)(end.tv_nsec - start.tv_nsec) / 1000000.0; }

static void pixel_hash(char digest[SHA256_HEX_LEN]) {
	sha256_ctx hash;
	sha256_init(&hash);
	sha256_update(&hash, pixels, sizeof(pixels));
	sha256_final_hex(&hash, digest);
}

int main(void) {
	// A live player keeps CPU priority. CPU time remains meaningful when the
	// benchmark is preempted; wall time also exposes that competing workload.
	if (setpriority(PRIO_PROCESS, 0, 19) != 0) {
		fprintf(stderr, "render bench: cannot lower process priority: %s\n", strerror(errno));
		return 1;
	}
	if (access(SONIX_RESOURCE_DIR "/fonts/ui-regular.ttf", R_OK) != 0 || access(SONIX_RESOURCE_DIR "/fonts/ui-semibold.ttf", R_OK) != 0) {
		fprintf(stderr, "render bench: installed UI Regular and Semibold TTF files are required\n");
		return 1;
	}
	lv_init();
	if (!fonts_init())
		return 1;
	lv_display_t *display = lv_display_create(WIDTH, HEIGHT);
	if (!display)
		return 1;
	lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(display, flush);
	lv_obj_t *screen = lv_display_get_screen_active(display);
	lv_obj_t *list = scene(screen);
	lv_obj_update_layout(screen);
	int max_scroll = ROWS * ROW_PITCH - 462;
	struct timespec cpu_start, cpu_end, wall_start, wall_end;
	char warmup_digest[SHA256_HEX_LEN];
	for (int frame = 0; frame < WARMUP_FRAMES + MEASURED_FRAMES; frame++) {
		if (frame == WARMUP_FRAMES) {
			pixel_hash(warmup_digest);
			if (!timestamp(CLOCK_MONOTONIC, &wall_start) || !timestamp(CLOCK_PROCESS_CPUTIME_ID, &cpu_start))
				return 1;
		}
		lv_tick_inc(16);
		int position = (frame * 73) % (max_scroll + 1);
		lv_obj_scroll_to_y(list, position, LV_ANIM_OFF);
		lv_obj_invalidate(screen);
		lv_refr_now(display);
		if (lv_obj_get_scroll_y(list) != position) {
			fprintf(stderr, "render bench: scroll position was not applied\n");
			return 1;
		}
	}
	if (!timestamp(CLOCK_PROCESS_CPUTIME_ID, &cpu_end) || !timestamp(CLOCK_MONOTONIC, &wall_end))
		return 1;
	if (bad_flush || flushes != WARMUP_FRAMES + MEASURED_FRAMES) {
		fprintf(stderr, "render bench: unexpected buffer/flush behavior (%u flushes)\n", flushes);
		return 1;
	}
	struct rusage usage;
	if (getrusage(RUSAGE_SELF, &usage) != 0)
		return 1;
	char digest[SHA256_HEX_LEN];
	pixel_hash(digest);
	if (strcmp(digest, warmup_digest) == 0) {
		fprintf(stderr, "render bench: distinct scroll positions produced an unchanged image\n");
		return 1;
	}
	double cpu_ms = elapsed_ms(cpu_start, cpu_end), wall_ms = elapsed_ms(wall_start, wall_end);
	printf("LTO_RENDER_BENCH frames=%d warmup=%d width=%d height=%d flushes=%u cpu_ms=%.3f wall_ms=%.3f cpu_frame_ms=%.3f maxrss_kib=%ld pixels_sha256=%s\n", MEASURED_FRAMES, WARMUP_FRAMES, WIDTH, HEIGHT, flushes, cpu_ms, wall_ms, cpu_ms / MEASURED_FRAMES, usage.ru_maxrss, digest);
	printf("LTO_RENDER_FONTS %s\n", fonts_summary());
	lv_display_delete(display);
	// Production fonts own process-lifetime FT descriptors; process exit
	// reclaims them together, as it does for the standalone player's shutdown.
	return 0;
}
