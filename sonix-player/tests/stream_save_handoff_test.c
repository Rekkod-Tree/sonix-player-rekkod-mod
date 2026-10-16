/*
 * Background-save pin lifetime across normal HTTP waits and cancellation.
 * Compile each real provider source; no service, card or decoded audio is used.
 * From sonix-player/, with LVGL headers available:
 * for service in YANDEX ZVUK; do
 *   cc -std=gnu11 -O1 -g -DHOST_BUILD -DSONIX_ZVUK -DTEST_$service \
 *     -DLV_CONF_INCLUDE_SIMPLE -I. -ffunction-sections -fdata-sections \
 *     tests/stream_save_handoff_test.c src/system/library/mp4flac.c \
 *     -Wl,--gc-sections -pthread -o /tmp/stream_save_handoff_test && \
 *     /tmp/stream_save_handoff_test || exit 1
 * done
 */
#ifdef TEST_YANDEX
#define _GNU_SOURCE 1
#else
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *fixture_fopen(const char *, const char *);
static int fixture_mkdir(const char *, mode_t);
static int fixture_stat(const char *, struct stat *);
#define fopen fixture_fopen
#define mkdir fixture_mkdir
#define stat(path, out) fixture_stat(path, out)
#ifdef TEST_YANDEX
#include "src/system/streaming/yandexdl.c"
#else
#include "src/system/streaming/zvukdl.c"
#endif
#undef fopen
#undef mkdir
#undef stat
static FILE *fixture_fopen(const char *path, const char *mode) {
	(void)path;
	(void)mode;
	assert(!"cancelled or failed HTTP must not open a late file");
	return NULL;
}
static int pin = -1, pin_count, metadata_calls, mkdir_calls;
static bool deny_pin, cancel_metadata, cancel_stream, existing_file;
int storage_pin_card(char *out, size_t size) {
	if (deny_pin)
		return -1;
	pin = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	assert(pin >= 0);
	pin_count++;
	snprintf(out, size, "/fixture/card");
	return pin;
}
bool storage_card_attached(void) { return true; }
bool usb_storage_active(void) { return false; }
bool storage_sd_writable(void) { return true; }
static int fixture_mkdir(const char *path, mode_t mode) {
	(void)mode;
	assert(fcntl(pin, F_GETFD) >= 0);
	assert(strncmp(path, "/fixture/card", 13) == 0);
	mkdir_calls++;
	return 0;
}
static int fixture_stat(const char *path, struct stat *st) {
	(void)path;
	memset(st, 0, sizeof(*st));
	if (existing_file) {
		st->st_size = 100;
		return 0;
	}
	errno = ENOENT;
	return -1;
}
const char *tr(const char *key) { return key; }
char **library_scan_folders(int *n) {
	*n = 0;
	return NULL;
}
void library_scan_folders_free(char **p, int n) {
	(void)p;
	(void)n;
}
void podcastdl_safe_name(const char *s, const char *fallback, char *out, size_t size) { snprintf(out, size, "%s", s[0] ? s : fallback); }
static void metadata_wait(void) {
	assert(fcntl(pin, F_GETFD) >= 0);
	metadata_calls++;
	if (cancel_metadata) {
		generation++;
		root[0] = '\0';
	}
}
#ifdef TEST_YANDEX
bool yandex_album_meta(const char *id, yandex_album_meta_t *out) {
	(void)id;
	(void)out;
	metadata_wait();
	return false;
}
const char *yandex_last_error(void) { return "fixture unavailable"; }
bool yandex_file_info(const char *id, yandex_file_t *out) {
	(void)id;
	(void)out;
	assert(fcntl(pin, F_GETFD) >= 0);
	if (cancel_stream) {
		generation++;
		root[0] = '\0';
		strcpy(out->codec, "mp3");
		return true;
	}
	return false;
}
bool yandexcache_find(const char *id, char *out, size_t size) {
	(void)id;
	(void)out;
	(void)size;
	return false;
}
yandex_quality_t yandex_get_quality(void) { return YANDEX_QUALITY_LOSSLESS; }
const char *yandexcache_extension(const char *codec) {
	(void)codec;
	return "mp3";
}
#else
int zvuk_album_tracks(const char *id, zvuk_track_t *out, int max, zvuk_album_t *album) {
	(void)id;
	(void)out;
	(void)max;
	(void)album;
	metadata_wait();
	return -1;
}
const char *zvuk_last_error(void) { return "fixture unavailable"; }
bool zvuk_stream(const char *id, zvuk_stream_t *out) {
	(void)id;
	(void)out;
	assert(fcntl(pin, F_GETFD) >= 0);
	if (cancel_stream) {
		generation++;
		root[0] = '\0';
		out->got = ZVUK_QUALITY_MID;
		return true;
	}
	return false;
}
bool zvukcache_find(const char *id, char *out, size_t size) {
	(void)id;
	(void)out;
	(void)size;
	return false;
}
zvuk_quality_t zvuk_get_quality(void) { return ZVUK_QUALITY_FLAC; }
#endif
bool http_get(const char *url, char **out, size_t *len, size_t cap, int timeout) {
	(void)url;
	(void)out;
	(void)len;
	(void)cap;
	(void)timeout;
	return false;
}
#ifdef TEST_YANDEX
void yandexsaved_add(const yandex_track_t *t, const char *artist, int year, int count, const char *path) {
	(void)t;
	(void)artist;
	(void)year;
	(void)count;
	(void)path;
	assert(!"unexpected index update");
}
void yandexcache_fetch_cancel(void) {}
bool yandexcache_fetch(const char *id, const yandex_file_t *file, FILE *out, void (*progress)(long, long, void *), void *user, uint64_t *samples, char *err, size_t size) {
#else
void zvukcache_fetch_cancel(void) {}
bool zvukcache_fetch(const char *id, const zvuk_stream_t *file, FILE *out, void (*progress)(long, long, void *), void *user, uint64_t *samples, char *err, size_t size) {
#endif
	(void)id;
	(void)file;
	(void)out;
	(void)progress;
	(void)user;
	(void)samples;
	(void)err;
	(void)size;
	assert(!"unexpected file download");
	return false;
}
static void run(bool no_pin, bool cancel_album, bool cancel_file) {
	deny_pin = no_pin;
	cancel_metadata = cancel_album;
	cancel_stream = cancel_file;
	metadata_calls = mkdir_calls = 0;
	generation = 1;
	strcpy(root, "/fixture/card");
	meta_ok = false;
#ifdef TEST_YANDEX
	yandex_track_t t = {0};
#else
	zvuk_track_t t = {0};
#endif
	strcpy(t.id, "track");
	strcpy(t.album_id, "album");
	strcpy(t.title, "Track");
	strcpy(t.album, "Album");
	strcpy(t.artist, "Artist");
	char why[256];
	assert(!save_track(&t, 1, why, sizeof(why)));
	assert(why[0]);
	if (no_pin) {
		assert(metadata_calls == 0 && mkdir_calls == 0);
	} else {
		assert(fcntl(pin, F_GETFD) < 0 && errno == EBADF);
		assert(metadata_calls == 1);
		if (cancel_album)
			assert(mkdir_calls == 0);
	}
}
int main(void) {
	run(true, false, false);
	run(false, true, false);
	run(false, false, false);
	run(false, false, true);
	assert(pin_count == 3);
	puts("PASS: save pin closes on all early failures; cancelled HTTP cannot create a late file");
	return 0;
}
