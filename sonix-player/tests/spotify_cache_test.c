// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/spotify_cache_test.c src/system/core/json.c
// src/system/decode/growfile.c -pthread -lm -Wl,--gc-sections -o /tmp/spotify_cache_test
#define _GNU_SOURCE
#include "src/system/streaming/spotify.c"
const char *tr(const char *key) { return key; }
#include <assert.h>

// Any playlist access from the trim worker is a test failure.
int playlist_current_index(void) { abort(); }
bool playlist_path_at(int i, char *p, size_t n) {
	(void)i;
	(void)p;
	(void)n;
	abort();
}
static void *trim_worker(void *p) {
	trim_cache(*(int *)p, "0000000000000000000003");
	return NULL;
}
int main(void) {
	char root[] = "/tmp/sonix-spotify-cache.XXXXXX";
	assert(mkdtemp(root));
	snprintf(cache_root, sizeof(cache_root), "%s", root);
	int dir = open(root, O_RDONLY | O_DIRECTORY);
	assert(dir >= 0);
	for (int i = 1; i <= 4; i++) {
		char id[23];
		snprintf(id, sizeof(id), "%022d", i);
		int track = child_directory(dir, id);
		assert(track >= 0);
		int fd = openat(track, "audio.ogg", O_WRONLY | O_CREAT, 0600);
		assert(fd >= 0);
		close(fd);
		close(track);
	}
	assert(spotify_track_path("0000000000000000000001", retained_current, sizeof(retained_current)));
	assert(spotify_track_path("0000000000000000000002", retained_next, sizeof(retained_next)));
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, trim_worker, &dir));
	pthread_join(thread, NULL);
	for (int i = 1; i <= 4; i++) {
		char id[23];
		snprintf(id, sizeof(id), "%022d", i);
		assert((faccessat(dir, id, F_OK, 0) == 0) == (i <= 3));
	}
	int track = openat(dir, "0000000000000000000001", O_RDONLY | O_DIRECTORY);
	assert(track >= 0);
	int tags = openat(track, "audio.ogg.tags", O_CREAT | O_WRONLY, 0600);
	assert(tags >= 0);
	close(tags);
	json_doc_t doc;
	assert(json_parse("{\"track_gain_db\":-3.5,\"track_peak\":0.9,\"album_gain_db\":2,\"album_peak\":1.2}", &doc));
	assert(store_gain_header(track, &doc));
	json_free(&doc);
	tags = openat(track, "audio.ogg.tags", O_RDONLY);
	char text[256] = {0};
	assert(read(tags, text, sizeof(text) - 1) > 0);
	close(tags);
	assert(strstr(text, "REPLAYGAIN_TRACK_GAIN=-3.5\n") && strstr(text, "REPLAYGAIN_ALBUM_PEAK=1.2\n"));
	unlinkat(track, "audio.ogg.tags", 0);
	close(track);
	for (int i = 1; i <= 3; i++) {
		char id[23];
		snprintf(id, sizeof(id), "%022d", i);
		track = openat(dir, id, O_RDONLY | O_DIRECTORY);
		unlinkat(track, "audio.ogg", 0);
		close(track);
		assert(!unlinkat(dir, id, AT_REMOVEDIR));
	}
	close(dir);
	assert(!rmdir(root));
	puts("PASS cache trim on worker preserves current/next without playlist access; gain header sidecar");
}
