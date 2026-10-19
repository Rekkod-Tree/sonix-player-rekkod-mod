// cc -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections -I. tests/playlist_service_test.c src/system/playback/playlist.c src/system/core/utils.c -Wl,--gc-sections -pthread -o /tmp/playlist-service-test && /tmp/playlist-service-test
#include "src/system/playback/playlist.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	const char *hidden[] = {"FOUND.000", "found.123", "Found.999", "LOST.DIR", "lost.dir",
		".DS_Store", "System Volume Information", "$RECYCLE.BIN", "._Song.flac"};
	const char *visible[] = {"Found.music", "FOUND.00", "FOUND.0000", "FOUND.00a", "LOST.DIR.music",
		"PEQ", "Screenshots", "Audiobooks", "Song.flac"};
	for (unsigned i = 0; i < sizeof(hidden) / sizeof(hidden[0]); i++)
		assert(playlist_is_junk_name(hidden[i]));
	for (unsigned i = 0; i < sizeof(visible) / sizeof(visible[0]); i++)
		assert(!playlist_is_junk_name(visible[i]));
	assert(playlist_is_playable_file("Song.flac"));
	assert(!playlist_is_playable_file("._Song.flac"));
	puts("PASS service names: FAT recovery, near misses, ordinary media and desktop entries");
	return 0;
}
