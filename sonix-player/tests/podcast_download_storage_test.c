/* From sonix-player: cc -std=gnu11 -Wall -Wextra -Werror -O1 -g
 * -fsanitize=address,undefined -fno-omit-frame-pointer -I.
 * tests/podcast_download_storage_test.c -pthread -o /tmp/podcast-storage-test
 * ASAN_OPTIONS=detect_leaks=1 /tmp/podcast-storage-test
 * Real downloader, synthetic card directories and deterministic network faults.
 */
#define _GNU_SOURCE
#include <pthread.h>
static pthread_t worker;
static int joinable_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
	(void)a;
	int rc = pthread_create(t, NULL, fn, arg);
	if (!rc)
		worker = *t;
	return rc;
}
#include <errno.h>
#include <unistd.h>
static int fail_write;
static ssize_t checked_write(int fd, const void *buf, size_t n) {
	if (fail_write) {
		errno = ENOSPC;
		return -1;
	}
	return write(fd, buf, n);
}
#define pthread_create joinable_create
#define write checked_write
#include "src/system/streaming/podcastdl.c"
#undef pthread_create
#undef write
#include <assert.h>
#include <dirent.h>

static char root[512];
static unsigned epoch = 1, notices;
static int reads, mode, pins, initial_fds;
static int fd_count(void);
enum { GOOD, NO_CARD, CONNECT_FAIL, TRUNCATED, NO_SPACE, CANCEL_CONNECT, REPLACE_CONNECT, REPLACE_BODY, REPLACE_COVER, LARGE };
static void replace(void) {
	char old[600], path[600];
	snprintf(old, sizeof(old), "%s-old", root);
	assert(!rename(root, old));
	epoch++;
	assert(!mkdir(root, 0700));
	snprintf(path, sizeof(path), "%s/Podcast", root);
	assert(!mkdir(path, 0700));
	snprintf(path, sizeof(path), "%s/Podcast/Show", root);
	assert(!mkdir(path, 0700));
	snprintf(path, sizeof(path), "%s/Podcast/Show/Episode.mp3", root);
	FILE *f = fopen(path, "wb");
	assert(f);
	assert(fwrite("KEEP", 1, 4, f) == 4);
	assert(!fclose(f));
}
const char *storage_sd_root(void) { return root; }
int storage_pin_card(char *out, size_t n) {
	pins++;
	if (mode == NO_CARD)
		return -1;
	snprintf(out, n, "%s", root);
	return open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
unsigned podcaststate_card_epoch(void) { return epoch; }
void podcaststate_downloaded(long long id, const char *p, unsigned e) {
	(void)id;
	(void)p;
	assert(e == epoch);
	notices++;
}
void thread_be_low_priority(const char *s) { (void)s; }
bool http_stream_open(http_stream_t *s, const char *url, int timeout) {
	(void)url;
	(void)timeout;
	assert(pins == 1);
	assert(fd_count() == initial_fds); /* Initial admission has closed before connecting. */
	if (mode == CANCEL_CONNECT)
		podcastdl_cancel();
	if (mode == REPLACE_CONNECT)
		replace();
	s->content_length = mode == TRUNCATED ? 99 : mode == LARGE ? 64 * DL_CHUNK : 8;
	return mode != CONNECT_FAIL;
}
int http_stream_read(http_stream_t *s, void *buf, int n) {
	(void)s;
	assert(n == DL_CHUNK && fd_count() == initial_fds);
	if (mode == LARGE) {
		if (reads++ == 64)
			return 0;
		memset(buf, 65, (size_t)n);
		return n;
	}
	if (mode == REPLACE_BODY && reads == 1)
		replace();
	if (reads++ == 2)
		return 0;
	memcpy(buf, "DATA", 4);
	return 4;
}
void http_stream_close(http_stream_t *s) { (void)s; }
void http_stream_wake(http_stream_t *s) { (void)s; }
bool http_get(const char *u, char **out, size_t *n, size_t limit, int timeout) {
	(void)u;
	(void)limit;
	(void)timeout;
	assert(fd_count() == initial_fds);
	if (mode == REPLACE_COVER)
		replace();
	*out = malloc(8);
	assert(*out);
	memcpy(*out, "\x89PNGTEST", 8);
	*n = 8;
	return true;
}
static int fd_count(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	while (readdir(d))
		n++;
	closedir(d);
	return n;
}
int main(int argc, char **argv) {
	alarm(30);
	char tmp[256];
	int len = snprintf(tmp, sizeof(tmp), "%s/sonix-podcast-storage-XXXXXX", argc > 1 ? argv[1] : "/tmp");
	assert(len > 0 && (size_t)len < sizeof(tmp));
	assert(mkdtemp(tmp));
	printf("fixtures=%s safe_card=%d\n", tmp, argc > 1);
	for (mode = GOOD; mode <= LARGE; mode++) {
		if (argc > 1 && mode != GOOD && mode != NO_SPACE && mode != CANCEL_CONNECT && mode != LARGE)
			continue;
		snprintf(root, sizeof(root), "%s/case%d", tmp, mode);
		assert(!mkdir(root, 0700));
		epoch++;
		reads = pins = notices = 0;
		fail_write = mode == NO_SPACE;
		podcast_episode_t e = {.id = 7, .published = 12345};
		snprintf(e.title, sizeof(e.title), "Episode");
		snprintf(e.enclosure, sizeof(e.enclosure), "fixture://body");
		int before = initial_fds = fd_count();
		bool started = podcastdl_start(&e, "Show", "Author", "fixture://cover");
		assert(started == (mode != NO_CARD));
		if (started)
			assert(!pthread_join(worker, NULL));
		assert(fd_count() == before);
		char path[700];
		snprintf(path, sizeof(path), "%s/Podcast/Show/Episode.mp3", root);
		if (mode >= REPLACE_CONNECT && mode <= REPLACE_COVER) {
			FILE *f = fopen(path, "rb");
			assert(f);
			char b[5] = {0};
			assert(fread(b, 1, 4, f) == 4);
			fclose(f);
			assert(!strcmp(b, "KEEP"));
			snprintf(path, sizeof(path), "%s/Podcast/Show/cover.png", root);
			assert(access(path, F_OK));
		} else if (mode == GOOD || mode == LARGE) {
			assert(!access(path, F_OK));
			assert(notices == 1);
			snprintf(path, sizeof(path), "%s/Podcast/Show/Episode.mp3.tags", root);
			assert(!access(path, F_OK));
			snprintf(path, sizeof(path), "%s/Podcast/Show/cover.png", root);
			assert(!access(path, F_OK));
		} else
			assert(access(path, F_OK));
		if (mode != GOOD && mode != LARGE && mode != REPLACE_COVER)
			assert(!notices);
		if (mode == LARGE)
			assert(pins == 69); // One pin per 32 KiB; no per-byte work.
		printf("PASS mode=%d pins=%d state=%d descriptors=%d\n", mode, pins, podcastdl_state(NULL, NULL), before);
		podcastdl_acknowledge();
	}
	return 0;
}
