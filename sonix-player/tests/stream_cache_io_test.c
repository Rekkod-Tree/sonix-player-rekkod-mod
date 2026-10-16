/*
Regression coverage for cache output errors and worker lifetime.
Real cache sources and MP4 writer; HTTP and thread scheduling are fixtures.
No device, service, credentials, or audio decoder is used.
Run from sonix-player/ on Linux (requires /dev/full):
  for service in YANDEX ZVUK; do
    cc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address \
      -ffunction-sections -fdata-sections -DSONIX_ZVUK -DTEST_$service -I. \
      tests/stream_cache_io_test.c src/system/library/mp4flac.c \
      -Wl,--gc-sections -pthread -o /tmp/sonix-cache-io-test && \
      /tmp/sonix-cache-io-test || exit 1
  done

*/
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
static int immediate_thread(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg);
#define pthread_create immediate_thread
#ifdef TEST_YANDEX
#include "src/system/streaming/yandexcache.c"
#define start yandexcache_start
#define fetch yandexcache_fetch
#define set_root yandexcache_set_root
#define find yandexcache_find
#define last_failure yandexcache_last_error
#else
#include "src/system/streaming/zvukcache.c"
#define start zvukcache_start
#define fetch zvukcache_fetch
#define set_root zvukcache_set_root
#define find zvukcache_find
#define last_failure zvukcache_last_error
#endif
#undef pthread_create

static unsigned char data[131072];
static long net_pos, net_size;
static bool growing;
static int thread_failure;
static int immediate_thread(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
	(void)t;
	(void)a;
	if (thread_failure)
		return thread_failure;
	fn(arg); // A fast worker can complete before pthread_create returns.
	return 0;
}
const char *tr(const char *key) { return key; }
void thread_be_low_priority(const char *name) { (void)name; }
int stream_provider_count(void) { return 0; }
const stream_provider_t *stream_provider_at(int i) {
	(void)i;
	return NULL;
}
void streamturn_acquire(void) {}
void streamturn_release(void) {}
void streamturn_register_abandon(void (*fn)(void)) { (void)fn; }
bool http_stream_open_range(http_stream_t *st, const char *url, long from, int timeout) {
	(void)url;
	(void)timeout;
	net_pos = from;
	st->total_length = net_size;
	return true;
}
bool http_stream_resume(http_stream_t *st, const char *url, long from, int timeout) { return http_stream_open_range(st, url, from, timeout); }
void http_stream_set_read_timeout(http_stream_t *st, int secs) {
	(void)st;
	(void)secs;
}
int http_stream_read(http_stream_t *st, void *out, int count) {
	(void)st;
	if (count > net_size - net_pos)
		count = (int)(net_size - net_pos);
	memcpy(out, data + net_pos, (size_t)count);
	net_pos += count;
	return count;
}
void http_stream_close(http_stream_t *st) {
	(void)st;
	errno = EBADF;
}
void http_stream_wake(http_stream_t *st) { (void)st; }
const char *http_last_error(void) { return "fixture network failure"; }
void growfile_progress(const char *path, long written) {
	(void)path;
	(void)written;
}
void growfile_announce(const char *path, long total) {
	(void)path;
	(void)total;
	growing = true;
}
void growfile_finish(const char *path, bool ok) {
	(void)path;
	(void)ok;
	growing = false;
}
bool growfile_is_growing(const char *path) {
	(void)path;
	return growing;
}
bool growfile_prefix_is_growing(const char *path) {
	(void)path;
	return growing;
}
#ifdef TEST_YANDEX
bool yandex_file_info(const char *id, yandex_file_t *out) {
	(void)id;
	(void)out;
	return false;
}
bool crypto_aes_ctr_available(size_t size) {
	(void)size;
	return false;
}
bool crypto_aes_ctr(const uint8_t *k, size_t n, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len, uint64_t off) {
	(void)k;
	(void)n;
	(void)iv;
	(void)in;
	(void)out;
	(void)len;
	(void)off;
	return false;
}
#else
bool zvuk_stream(const char *id, zvuk_stream_t *out) {
	(void)id;
	(void)out;
	return false;
}
const char *zvuk_stream_headers(void) { return ""; }
void http_set_thread_stream_headers(const char *s) { (void)s; }
void http_last_outcome(http_outcome_t *o) { memset(o, 0, sizeof(*o)); }
#endif

static void run_fetch(bool full, bool unbuffered, long bytes) {
	net_size = bytes;
	net_pos = 0;
	FILE *out = full ? fopen("/dev/full", "wb") : tmpfile();
	assert(out);
	if (unbuffered)
		assert(setvbuf(out, NULL, _IONBF, 0) == 0);
	char error[256];
#ifdef TEST_YANDEX
	yandex_file_t file = {0};
	strcpy(file.codec, "mp3");
#else
	zvuk_stream_t file = {0};
	file.got = ZVUK_QUALITY_MID;
#endif
	bool ok = fetch("fixture", &file, out, NULL, NULL, NULL, error, sizeof(error));
	if (full) {
		assert(!ok);
		assert(strstr(error, strerror(ENOSPC)));
	} else {
		assert(ok);
		assert(!error[0]);
		assert(ftell(out) == bytes);
		rewind(out);
		unsigned char copy[sizeof(data)];
		assert(fread(copy, 1, (size_t)bytes, out) == (size_t)bytes);
		assert(memcmp(copy, data, (size_t)bytes) == 0);
	}
	fclose(out);
}
static void run_payload(bool full, long bytes) {
	net_size = bytes;
	net_pos = 0;
	FILE *out = full ? fopen("/dev/full", "wb") : tmpfile();
	assert(out);
	download_t d = {0};
	d.f = out;
	d.detached = true;
	mp4flac_t u = {0};
	u.out = out;
	u.unwrap = true;
#ifdef TEST_YANDEX
	bool ok = pass_through(&d, &u, bytes);
#else
	unsigned char buf[BUF_SIZE];
	bool ok = frames_through(&d, &u, buf, bytes);
#endif
	if (full) {
		assert(!ok);
		assert(strstr(d.error, strerror(ENOSPC)));
		assert(d.written == 0);
	} else {
		assert(ok);
		assert(d.written == bytes);
		assert(ftell(out) == bytes);
	}
	fclose(out);
}
int main(void) {
	memset(data, 'A', sizeof(data));
	run_payload(false, sizeof(data));
	run_payload(true, sizeof(data));
	run_payload(true, 8);
	run_fetch(false, false, sizeof(data));
	run_fetch(true, false, sizeof(data));
	run_fetch(true, false, 8); // Failure only when the small buffered file is flushed.
	run_fetch(true, true, sizeof(data));
	char root[] = "/tmp/sonix-cache-fixture-XXXXXX";
	assert(mkdtemp(root));
	set_root(root);
#ifdef TEST_YANDEX
	yandex_track_t track = {0};
	yandex_file_t file = {0};
	strcpy(file.codec, "mp3");
#else
	zvuk_track_t track = {0};
	zvuk_stream_t file = {0};
	file.got = ZVUK_QUALITY_MID;
#endif
	strcpy(track.id, "fixture");
	net_size = sizeof(data);
	net_pos = 0;
	char out[512];
	assert(start(&track, &file, out, sizeof(out)));
	assert(out[0]);
	assert(find(track.id, NULL, 0));
	assert(unlink(out) == 0);
	thread_failure = EAGAIN;
	strcpy(out, "stale");
	assert(!start(&track, &file, out, sizeof(out)));
	assert(!out[0]);
	assert(strstr(last_failure(), strerror(EAGAIN)));
	assert(!current && !growing);
	char local[512];
	snprintf(local, sizeof(local), "%s/.local", root);
	assert(rmdir(cache_dir) == 0);
	assert(rmdir(local) == 0);
	assert(rmdir(root) == 0);
	puts("PASS: successful fetch, full-card fwrite/flush, fast worker completion, thread failure cleanup");
	return 0;
}
