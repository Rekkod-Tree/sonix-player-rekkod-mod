/* Normal metadata snapshots, no device, audio, network or card writes.
 * cc -std=gnu11 -ffunction-sections -fdata-sections -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl
 *   tests/stream_metadata_state_test.c src/system/streaming/provider.c
 *   -Wl,--gc-sections -pthread -lm -o /tmp/stream_metadata_state_test
 * Artist display and the history sink are explicit fixture stubs. This checks
 * their invocation contract, not metadata.c's artist-preference policy.
 */
#include "src/system/playback/device_state.c"
#include <assert.h>

static song_metadata_t file_tags, api_tags;
static bool api_available;
static unsigned reads;
static unsigned history_updates;
static bool owned(const char *path) { return path && strncmp(path, "/cache/", 7) == 0; }
static bool peek(const char *path, song_metadata_t *out) {
	(void)path;
	*out = api_tags;
	return api_available;
}
void metadata_read(const char *path, song_metadata_t *out) {
	reads++;
	assert(strcmp(path, "/cache/123.m4a") == 0);
	*out = file_tags;
}
const char *metadata_shown_artist(const song_metadata_t *metadata) {
	assert(metadata == &current_metadata && metadata->compilation);
	assert(strcmp(metadata->album_artist, "Various artists") == 0);
	return metadata->artist;
}
void playhistory_metadata(const char *path, const char *title, const char *artist) {
	assert(strcmp(path, "/cache/123.m4a") == 0);
	assert(strcmp(title, "A normal track") == 0);
	assert(strcmp(artist, "Artist") == 0);
	history_updates++;
}
bool library_track_album_value(const char *path, char *out, size_t size) {
	(void)path;
	(void)out;
	(void)size;
	return false;
}

int main(void) {
	stream_provider_t provider = {.id = "test", .owns_path = owned, .is_track = owned, .peek_metadata = peek};
	stream_provider_register(&provider);
	snprintf(current_metadata_file, sizeof(current_metadata_file), "/cache/123.m4a");
	file_tags = (song_metadata_t){.has_stream = true, .alac = true, .sample_rate = 44100, .bits_per_sample = 24, .duration_ms = 123000, .has_track_gain = true, .track_gain_db = -7.5f, .track_peak = 0.9f, .compilation = true};
	snprintf(file_tags.album_artist, sizeof(file_tags.album_artist), "Various artists");
	api_available = true;
	snprintf(api_tags.title, sizeof(api_tags.title), "A normal track");
	snprintf(api_tags.artist, sizeof(api_tags.artist), "Artist");
	snprintf(api_tags.album, sizeof(api_tags.album), "Album");
	read_current_metadata();
	song_metadata_t expected = file_tags;
	snprintf(expected.title, sizeof(expected.title), "%s", api_tags.title);
	snprintf(expected.artist, sizeof(expected.artist), "%s", api_tags.artist);
	snprintf(expected.album, sizeof(expected.album), "%s", api_tags.album);
	expected.has_tags = true;
	assert(memcmp(&current_metadata, &expected, sizeof(expected)) == 0);
	char loaded_album[300], next_album[300];
	album_key(current_metadata_file, &current_metadata, loaded_album, sizeof(loaded_album));
	album_key(current_metadata_file, NULL, next_album, sizeof(next_album));
	assert(strcmp(loaded_album, next_album) == 0);

	// Missing tags at load; the same path acquires its title later, without
	// rereading the audio or replacing ReplayGain/format/compilation fields.
	api_available = false;
	read_current_metadata();
	assert(!current_metadata.title[0]);
	api_available = true;
	assert(!device_state_refresh_metadata("/cache/456.m4a"));
	assert(!current_metadata.title[0]);
	assert(history_updates == 0);
	unsigned before = device_state_metadata_revision();
	unsigned reads_before = reads;
	device_state_note_metadata_available(current_metadata_file);
	assert(reads == reads_before);
	assert(device_state_metadata_revision() == before + 1);
	assert(memcmp(&current_metadata, &expected, sizeof(expected)) == 0);
	assert(history_updates == 1);
	device_state_note_metadata_available(current_metadata_file);
	assert(history_updates == 1); // Already filled: no duplicate history update.

	// A failed or empty peek, and a different API title, cannot erase good tags.
	api_available = false;
	memset(&api_tags, 0, sizeof(api_tags));
	assert(!stream_provider_fill_metadata(current_metadata_file, &current_metadata));
	api_available = true;
	assert(!stream_provider_fill_metadata(current_metadata_file, &current_metadata));
	snprintf(api_tags.title, sizeof(api_tags.title), "Different title");
	assert(!stream_provider_fill_metadata(current_metadata_file, &current_metadata));
	assert(memcmp(&current_metadata, &expected, sizeof(expected)) == 0);
	puts("PASS: loaded cache title, same-path late refresh, exact-path guard, lossless display merge, one matching history update");
	return 0;
}
