// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I. tests/sidecar_gain_test.c
// src/system/audio/replaygain.c -lm -Wl,--gc-sections -o /tmp/sidecar_gain_test
#define _GNU_SOURCE
#include "src/system/audio/replaygain.h"
#include "src/system/library/metadata.c"
#include <assert.h>
#include <math.h>
#include <unistd.h>
static long mode;
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	return !strcmp(key, "replaygain") ? mode : fallback;
}
int main(void) {
	char root[] = "/tmp/sonix-gain.XXXXXX";
	assert(mkdtemp(root));
	char audio[512], path[520];
	snprintf(audio, sizeof(audio), "%s/audio.ogg", root);
	snprintf(path, sizeof(path), "%s.tags", audio);
	FILE *f = fopen(path, "w");
	assert(f);
	fputs("title=API title\nREPLAYGAIN_TRACK_GAIN=-3.5\nREPLAYGAIN_TRACK_PEAK=0.9\nREPLAYGAIN_ALBUM_GAIN=6\nREPLAYGAIN_ALBUM_PEAK=2\n", f);
	fclose(f);
	song_metadata_t meta = {0};
	read_sidecar_tags(audio, &meta);
	assert(!strcmp(meta.title, "API title") && meta.has_track_gain && meta.has_album_gain);
	assert(fabs(meta.track_gain_db + 3.5) < 1e-6 && fabs(meta.album_peak - 2) < 1e-6);
	mode = REPLAYGAIN_OFF;
	replaygain_load(&meta);
	assert(!replaygain_active());
	mode = REPLAYGAIN_TRACK;
	replaygain_load(&meta);
	assert(fabs(replaygain_current_db() + 3.5) < 1e-6);
	mode = REPLAYGAIN_ALBUM;
	replaygain_load(&meta);
	assert(fabs(replaygain_current_db() + 6.020599913) < 1e-6);
	assert(!unlink(path));
	assert(!rmdir(root));
	puts("PASS sidecar gain -> shared ReplayGain; disabled, track, album clipping guard");
}
