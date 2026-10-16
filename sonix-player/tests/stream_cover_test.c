/* Real sidecar/cache code with HTTP and storage admission fixtures.
 * From sonix-player/, for each service YANDEX and ZVUK:
 * cc -std=gnu11 -O1 -Wall -Wextra -ffunction-sections -fdata-sections
 * -DSONIX_ZVUK -DTEST_YANDEX -I. tests/stream_cover_test.c
 * src/system/image/stb_image_impl.c -Wl,--gc-sections -pthread -lm
 * -o /tmp/stream-cover-test && /tmp/stream-cover-test
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
static int test_close(int fd);
static int test_fclose(FILE *f);
static int test_rename(const char *from, const char *to);
#define close test_close
#ifdef TEST_YANDEX
#include "src/system/streaming/yandexcache.c"
#define set_root yandexcache_set_root
#define write_sidecars yandexcache_write_sidecars
#define abandon yandexcache_abandon_all
#define track_t yandex_track_t
#define CACHE_SUBDIR YANDEXCACHE_DIR
#else
#include "src/system/streaming/zvukcache.c"
#define set_root zvukcache_set_root
#define write_sidecars zvukcache_write_sidecars
#define abandon zvukcache_abandon_all
#define track_t zvuk_track_t
#define CACHE_SUBDIR ZVUKCACHE_DIR
#endif
#define fclose test_fclose
#define rename test_rename
#include "src/system/streaming/streamcover.c"
#undef fclose
#undef rename
#undef close

// A normal 1x1 PNG; generated image data, no network fixture.
static const unsigned char png[] = {
  137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
  0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,0,0,0,0,73,69,78,68,174,66,96,130
};
// Normal JPEG generated from an 8x8 solid RGB image.
static const unsigned char jpeg[] = {
	255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,
	255,219,0,67,0,8,6,6,7,6,5,8,7,7,7,9,9,8,10,12,
	20,13,12,11,11,12,25,18,19,15,20,29,26,31,30,29,26,28,28,32,
	36,46,39,32,34,44,35,28,28,40,55,41,44,48,49,52,52,52,31,39,
	57,61,56,50,60,46,51,52,50,255,219,0,67,1,9,9,9,12,11,12,
	24,13,13,24,50,33,28,33,50,50,50,50,50,50,50,50,50,50,50,50,
	50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,
	50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,255,192,
	0,17,8,0,8,0,8,3,1,34,0,2,17,1,3,17,1,255,196,0,
	31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,
	2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,
	2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,
	49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,
	82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,
	42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,
	87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,
	131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,
	165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,
	199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,
	232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,
	1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,
	6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,
	5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,
	7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,
	98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,
	54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,
	90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,
	133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,
	167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,
	201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,
	242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,
	0,63,0,201,162,138,43,214,60,243,255,217,
};
static char card[512];
static int pin_fd = -1, requests, mode;
static bool deny, fail_close, fail_rename;
void http_stream_wake(http_stream_t *stream) { (void)stream; }

int storage_pin_card(char *out, size_t size) {
	assert(pin_fd < 0);
	if (deny) return -1;
	snprintf(out, size, "%s", card);
	pin_fd = open(card, O_RDONLY | O_DIRECTORY);
	return pin_fd;
}
static int test_close(int fd) {
	if (fd == pin_fd) pin_fd = -1;
	return close(fd);
}
static int test_fclose(FILE *f) {
	int rc = fclose(f);
	if (fail_close) { errno = EIO; return EOF; }
	return rc;
}
static int test_rename(const char *a, const char *b) {
	if (fail_rename) { errno = EROFS; return -1; }
	return rename(a, b);
}
static bool response(char **out, size_t *len) {
	assert(pin_fd < 0); // Network must not pin the SD mount.
	requests++;
	if (mode == 1) return false;
	if (mode == 2) abandon();
	if (mode == 3) deny = true;
	if (mode == 4) set_root(NULL);
	*len = sizeof(png);
	*out = malloc(*len);
	assert(*out);
	memcpy(*out, png, *len);
	return true;
}
#ifdef TEST_YANDEX
bool http_get(const char *url, char **out, size_t *len, size_t limit, int timeout) {
	(void)url; (void)limit; (void)timeout;
	return response(out, len);
}
#else
const char *zvuk_stream_headers(void) { return ""; }
bool http_get_ex(const char *url, const char *headers, bool errors, char **out, size_t *len,
				 size_t limit, int timeout, int *status) {
	(void)url; (void)headers; (void)errors; (void)limit; (void)timeout; (void)status;
	return response(out, len);
}
#endif

int main(void) {
	char tmp[] = "/tmp/sonix-cover-XXXXXX", path[600], jpg[600], tags[610];
	assert(mkdtemp(tmp));
	snprintf(card, sizeof(card), "%s", tmp);
	set_root(card);
	snprintf(path, sizeof(path), "%s/" CACHE_SUBDIR "/42.mp3", card);
	snprintf(jpg, sizeof(jpg), "%s/" CACHE_SUBDIR "/42.jpg", card);
	snprintf(tags, sizeof(tags), "%s.tags", path);
	assert(streamcover_valid(png, sizeof(png)));
	assert(streamcover_valid(jpeg, sizeof(jpeg)));
	assert(!streamcover_valid(jpeg, sizeof(jpeg) - 10));
	assert(!streamcover_valid("<html>unavailable</html>", 24));
	assert(!streamcover_valid(png, sizeof(png) - 8));
	assert(streamcover_store(jpg, jpeg, sizeof(jpeg)));
	assert(streamcover_file_valid(jpg));
	fail_close = true;
	assert(!streamcover_store(jpg, png, sizeof(png)));
	fail_close = false;
	assert(streamcover_file_valid(jpg));
	fail_rename = true;
	assert(!streamcover_store(jpg, png, sizeof(png)));
	fail_rename = false;
	assert(streamcover_file_valid(jpg));
	track_t t = {0};
	strcpy(t.id, "42"); strcpy(t.title, "Fixture"); strcpy(t.cover, "https://example.invalid/cover");
	write_sidecars(&t, path);
	assert(requests == 0 && pin_fd < 0);
	t.title[0] = '\0';
	write_sidecars(&t, path);
	FILE *saved_tags = fopen(tags, "r");
	char first_line[80];
	assert(saved_tags && fgets(first_line, sizeof(first_line), saved_tags));
	assert(strcmp(first_line, "title=Fixture\n") == 0 && fclose(saved_tags) == 0);
	strcpy(t.title, "Fixture");
	assert(unlink(jpg) == 0);
	for (mode = 1; mode <= 4; mode++) {
		write_sidecars(&t, path);
		assert(access(jpg, F_OK) != 0 && pin_fd < 0);
		deny = false;
		set_root(card);
	}
	mode = 0;
	write_sidecars(&t, path);
	assert(streamcover_file_valid(jpg) && pin_fd < 0);
	int before = requests;
	write_sidecars(&t, path);
	assert(requests == before);
	// An interrupted older write must not suppress the next cover request.
	FILE *f = fopen(jpg, "wb"); assert(f);
	assert(fwrite(png, 1, 20, f) == 20 && fclose(f) == 0);
	write_sidecars(&t, path);
	assert(requests == before + 1 && streamcover_file_valid(jpg));
	assert(unlink(jpg) == 0 && unlink(tags) == 0);
	char dir[600]; snprintf(dir, sizeof(dir), "%s/" CACHE_SUBDIR, card);
	assert(rmdir(dir) == 0); // Also proves no failed-write temp files remain.
	snprintf(dir, sizeof(dir), "%s/.local", card);
	assert(rmdir(dir) == 0 && rmdir(card) == 0);
	puts("stream cover publication/admission: PASS");
}
