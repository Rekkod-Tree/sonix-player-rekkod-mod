// Compile the actual player preview/commit functions with inert audio state.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "src/gui/nowplaying/player.h"
#include "src/system/device/power.h"
#include "src/system/playback/audiobook.h"
#include "src/system/playback/device_state.h"

static device_state_t state;
static unsigned revision = 1, seeks, book_notes;
static bool lit = true, audiobook_mode, audiobook_has_chapters;
static double view_start, view_length, last_seek, last_book_note;

void device_state_get(device_state_t *out) { *out = state; }
unsigned device_state_metadata_revision(void) { return revision; }
bool power_screen_is_on(void) { return lit; }
void device_state_seek(double position) {
	seeks++;
	last_seek = position;
}
bool audiobook_is_playing(void) { return audiobook_mode; }
bool audiobook_duration_per_chapter(void) { return true; }
int audiobook_chapter_at(double seconds) { return seconds >= 120 ? 1 : 0; }
bool audiobook_chapter(int index, char *title, size_t size, double *start) {
	(void)title;
	(void)size;
	if (index < 0 || index > 2)
		return false;
	*start = index * 120.0;
	return true;
}
void audiobook_note_position(double seconds, double total, bool force) {
	assert(force && total == state.progress_total_secs);
	book_notes++;
	last_book_note = seconds;
}

#include "actual_mini_seek.inc"

int main(void) {
	state.status = AUDIO_STATUS_PLAYING;
	snprintf(state.current_file, sizeof(state.current_file), "/fixture/track.flac");
	state.progress_current_secs = 77;
	state.progress_total_secs = 258;
	player_mini_seek_t range;
	assert(player_mini_seek_begin(&range) && seeks == 0);
	assert(range.position == 77 && range.start == 0 && range.length == 258);
	assert(player_mini_seek_commit(&range, 102) && seeks == 1 && last_seek == 102 && book_notes == 0);
	state.status = AUDIO_STATUS_PAUSED;
	assert(player_mini_seek_begin(&range));
	assert(player_mini_seek_commit(&range, -1) && last_seek == 0 && state.status == AUDIO_STATUS_PAUSED);
	assert(player_mini_seek_commit(&range, 999) && last_seek < 258 && last_seek > 257);
	unsigned before = seeks;
	assert(!player_mini_seek_commit(&range, NAN) && seeks == before);
	assert(!player_mini_seek_commit(&range, INFINITY) && seeks == before);
	revision++;
	assert(!player_mini_seek_valid(&range));
	assert(!player_mini_seek_commit(&range, 50) && seeks == before);
	assert(player_mini_seek_begin(&range));
	lit = false;
	assert(!player_mini_seek_begin(&range) && !player_mini_seek_commit(&range, 50));
	lit = true;
	state.live = true;
	assert(!player_mini_seek_begin(&range) && !player_mini_seek_commit(&range, 50));
	state.live = false;
	state.status = AUDIO_STATUS_STOPPED;
	assert(!player_mini_seek_begin(&range) && !player_mini_seek_commit(&range, 50));
	state.status = AUDIO_STATUS_PLAYING;
	state.progress_total_secs = 0;
	assert(!player_mini_seek_begin(&range));
	state.progress_total_secs = INFINITY;
	assert(!player_mini_seek_begin(&range));
	state.progress_total_secs = 1e100;
	assert(!player_mini_seek_begin(&range));
	state.progress_total_secs = 258;
	state.progress_current_secs = NAN;
	assert(!player_mini_seek_begin(&range));
	assert(seeks == before);
	audiobook_mode = true;
	audiobook_has_chapters = true;
	state.progress_current_secs = 130;
	state.progress_total_secs = 600;
	assert(player_mini_seek_begin(&range));
	assert(range.start == 120 && range.length == 120);
	assert(player_mini_seek_commit(&range, 10) && last_seek == 120 && last_book_note == 120);
	assert(player_mini_seek_commit(&range, 1000) && last_seek < 240 && last_seek > 239);
	assert(book_notes == 2 && last_book_note == last_seek);
	puts("PASS actual mini seek guards, paused state, chapter range, metadata generation, unknown duration and single commit boundary");
	return 0;
}
