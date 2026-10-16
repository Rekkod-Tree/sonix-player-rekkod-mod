// CLI for normal-file metadata/decoder regressions. metadata_normal_test.py
// links this against the host production objects with a wrapped main entry.
// It reads files only, never opens an audio device or starts the interface.
#include "src/system/decode/decode.h"
#include "src/system/library/cue.h"
#include "src/system/library/metadata.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void string(const char *text) {
	putchar('"');
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (*p == '"' || *p == '\\')
			printf("\\%c", *p);
		else if (*p < 32)
			printf("\\u%04x", *p);
		else
			putchar(*p);
	}
	putchar('"');
}
static void field(const char *name, const char *value) {
	string(name);
	putchar(':');
	string(value);
	putchar(',');
}
int main(int argc, char **argv) {
	if (argc < 3)
		return 2;
	if (strcmp(argv[1], "meta") == 0) {
		song_metadata_t m;
		metadata_read(argv[2], &m);
		printf("RESULT {");
		field("title", m.title);
		field("artist", m.artist);
		field("album", m.album);
		field("album_artist", m.album_artist);
		field("genre", m.genre);
		field("series", m.series);
		field("series_part", m.series_part);
		printf("\"track\":%d,\"disc\":%d,\"year\":%d,\"has_tags\":%d,\"has_stream\":%d,\"alac\":%d,\"rate\":%d,\"bits\":%d,\"duration_ms\":%u,\"has_track_gain\":%d,\"has_album_gain\":%d,\"track_gain\":%.5f,\"album_gain\":%.5f,\"format\":%d}\n", m.track_number, m.disc_number, m.year, m.has_tags, m.has_stream, m.alac, m.sample_rate, m.bits_per_sample, m.duration_ms, m.has_track_gain, m.has_album_gain, m.track_gain_db, m.album_gain_db, (int)decode_detect_format(argv[2]));
		return 0;
	}
	if (strcmp(argv[1], "cue") == 0) {
		cue_sheet_t *s = calloc(1, sizeof(*s));
		if (!s || !cue_parse(argv[2], s)) {
			free(s);
			return 3;
		}
		printf("RESULT {\"audio\":");
		string(s->audio_path);
		printf(",\"tracks\":%d}\n", s->track_count);
		free(s);
		return 0;
	}
	if (strcmp(argv[1], "pcm") == 0) {
		decode_format_t format = decode_detect_format(argv[2]);
		decoder_t *d = decoder_open(argv[2], format);
		if (!d)
			return 4;
		uint64_t start = argc > 3 ? strtoull(argv[3], NULL, 10) : 0;
		if (start && !decoder_seek_to_frame(d, start)) {
			decoder_close(d);
			return 5;
		}
		int channels = decoder_channels(d);
		if (channels < 1 || channels > 8) {
			decoder_close(d);
			return 6;
		}
		int32_t samples[256 * 8];
		uint64_t frames = decoder_read_pcm_frames_s32(d, 256, samples), hash = UINT64_C(14695981039346656037);
		for (uint64_t i = 0; i < frames * (uint64_t)channels; i++) {
			uint32_t sample = (uint32_t)samples[i];
			for (int b = 0; b < 4; b++) {
				hash ^= (sample >> (8 * b)) & 255;
				hash *= UINT64_C(1099511628211);
			}
		}
		printf("RESULT {\"frames\":%" PRIu64 ",\"hash\":\"%016" PRIx64 "\",\"rate\":%d,\"channels\":%d,\"bits\":%d}\n", frames, hash, decoder_sample_rate(d), channels, decoder_source_bits(d));
		decoder_close(d);
		return frames ? 0 : 7;
	}
	return 2;
}
