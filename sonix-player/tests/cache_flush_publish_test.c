// Build three times with -DCACHE_QOBUZ, -DCACHE_TIDAL or -DCACHE_SUBSONIC.
// cc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -I. -DCACHE_QOBUZ tests/cache_flush_publish_test.c -Wl,--gc-sections -pthread -o /tmp/cache_flush
#define _GNU_SOURCE
#include <assert.h>
#ifdef CACHE_QOBUZ
#include "src/system/streaming/qobuzcache.c"
#elif defined(CACHE_TIDAL)
#include "src/system/streaming/tidalcache.c"
#else
#include "src/system/streaming/subsoniccache.c"
#endif

static int publications;
int http_stream_read(http_stream_t *st, void *buf, int size) {
	(void)st; assert(size >= 4); memcpy(buf, "DATA", 4); return 4;
}
bool streamturn_resume(const streamturn_resume_t *request, int *attempts) { (void)request; (void)attempts; assert(false); return false; }
void growfile_progress(const char *path, long done) { (void)path; assert(done == 4); publications++; }
#ifdef CACHE_TIDAL
bool tidal_dash_segment_url(const tidal_stream_t *stream, int number, char *out, size_t size) {
	(void)stream; (void)number; snprintf(out, size, "fixture"); return true;
}
bool http_get(const char *url, char **out, size_t *size, size_t limit, int timeout) {
	(void)url; (void)limit; (void)timeout; *out = strdup("DATA"); *size = 4; return true;
}
bool mp4flac_segment(mp4flac_t *state, const uint8_t *buf, size_t size) {
	assert(fwrite(buf, 1, size, state->out) == size); state->written += size; return true;
}
#endif
static void check(bool fail, bool dash) {
	download_t d = {0}; d.f = fail ? fopen("/dev/full", "wb") : tmpfile(); assert(d.f);
	// Make fwrite succeed, exposing the later, distinct fflush failure.
	char buffer[4096]; assert(!setvbuf(d.f, buffer, _IOFBF, sizeof(buffer)));
	d.generation = current_generation(); publications = 0; bool finished = false, ok;
#ifdef CACHE_TIDAL
	if (dash) { d.stream.segment_count = 1; d.unwrap.out = d.f; ok = pump_dash(&d, 1, &finished); }
	else ok = pump_file(&d, 4, &finished);
#else
	(void)dash; ok = download_pump(&d, 4, &finished);
#endif
	assert(ok == !fail); assert(d.done == (fail ? 0 : 4)); assert(publications == (fail ? 0 : 1));
	fclose(d.f);
}
int main(void) {
	check(false, false); check(true, false);
#ifdef CACHE_TIDAL
	check(false, true); check(true, true);
#endif
	puts("PASS failed flush cannot publish growing-file bytes");
}
