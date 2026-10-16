/*
 * The real queue/Up next name resolvers must show lazy service metadata
 * before an audio file exists. No LVGL runtime or service is started.
 * From sonix-player/ on Linux:
 * for view in PLAYER QUEUE; do
 *   cc -std=gnu11 -O1 -g -DHOST_BUILD -DSONIX_ZVUK -DTEST_$view \
 *     -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl -ffunction-sections -fdata-sections \
 *     tests/stream_metadata_ui_test.c src/system/streaming/provider.c \
 *     -Wl,--gc-sections -pthread -o /tmp/stream-metadata-ui-test && \
 *     /tmp/stream-metadata-ui-test || exit 1
 * done
 */
#include <assert.h>
#ifdef TEST_PLAYER
#include "src/gui/nowplaying/player.c"
#else
#include "src/gui/nowplaying/trackmenu.c"
#endif

static bool details_ready;
static int file_reads;
static bool fixture_owns(const char *path) { return strncmp(path, "/fixture-cache/", 15) == 0; }
static bool fixture_metadata(const char *path, song_metadata_t *out) {
	if (!fixture_owns(path) || !details_ready)
		return false;
	snprintf(out->title, sizeof(out->title), "Actual track title");
	snprintf(out->artist, sizeof(out->artist), "Actual artist");
	snprintf(out->album, sizeof(out->album), "Actual album");
	return true;
}
bool library_track_title(const char *path, char *out, size_t size) {
	(void)path;
	(void)out;
	(void)size;
	return false;
}
bool library_track_names(const char *path, char *title, size_t title_size, char *artist, size_t artist_size) {
	(void)path;
	(void)title;
	(void)title_size;
	(void)artist;
	(void)artist_size;
	return false;
}
void metadata_read(const char *path, song_metadata_t *out) {
	file_reads++;
	memset(out, 0, sizeof(*out));
	// Models a sidecar already published while its audio file is absent.
	if (strcmp(path, "/fixture-cache/sidecar.m4a") == 0) {
		snprintf(out->title, sizeof(out->title), "Sidecar title");
	}
}
const char *metadata_shown_artist(const song_metadata_t *meta) { return meta->artist; }

static void resolve(const char *path, char *title) {
#ifdef TEST_PLAYER
	np_title_for_path(path, title, 256);
#else
	char artist[256], album[256];
	queue_names_of(path, title, 256, artist, sizeof(artist), album, sizeof(album));
	if (details_ready) {
		assert(strcmp(artist, "Actual artist") == 0);
		assert(strcmp(album, "Actual album") == 0);
	}
#endif
}
int main(void) {
	stream_provider_t service = {.id = "fixture", .owns_path = fixture_owns, .is_track = fixture_owns, .peek_metadata = fixture_metadata};
	stream_provider_register(&service);
	char title[256];
	details_ready = true;
	resolve("/fixture-cache/144069784.m4a", title);
	assert(strcmp(title, "Actual track title") == 0);
	assert(file_reads == 0);
	// Codec-path correction still resolves to the same service title.
	resolve("/fixture-cache/144069784.flac", title);
	assert(strcmp(title, "Actual track title") == 0);
	assert(file_reads == 0);
	details_ready = false;
	resolve("/fixture-cache/sidecar.m4a", title);
	assert(strcmp(title, "Sidecar title") == 0);
	assert(file_reads == 1);
	resolve("/ordinary/Local song.flac", title);
	assert(strcmp(title, "Local song") == 0);
	puts("PASS: lazy service title, codec replacement, sidecar before audio, local fallback");
	return 0;
}
