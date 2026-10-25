// Production failure policy; no sound device, card, decoder or worker starts.
// cc -std=gnu11 -O2 -ffunction-sections -fdata-sections -I. -Ilvgl
//   tests/audio_output_failure_test.c -Wl,--gc-sections -lasound -pthread -lm -o /tmp/audio_output_failure_test
#define _GNU_SOURCE
#include "src/system/audio/audio.c"
#include <assert.h>

static void reset(void) {
	play_request = stop_thread = seek_request = false;
	audio_command = AUDIO_CMD_PLAY;
	playback_status = AUDIO_STATUS_PLAYING;
	progress_current_secs = 92.25;
	progress_total_secs = 179.0;
	strcpy(current_filepath, "/card/book/chapter.flac");
	seek_target_secs = -1;
	track_completed = false;
}

static void unchanged_source(void) {
	assert(progress_current_secs == 92.25 && progress_total_secs == 179.0);
	assert(!strcmp(current_filepath, "/card/book/chapter.flac"));
	assert(!track_completed);
}

int main(void) {
	reset();
	pause_failed_output();
	assert(audio_command == AUDIO_CMD_PAUSE && playback_status == AUDIO_STATUS_PAUSED);
	assert(!play_request && !seek_request);
	unchanged_source();
	// Reopening an output must publish pause and position in one command.
	reset();
	audio_play_paused("/card/book/chapter.flac", 92.5);
	assert(play_request && seek_request && seek_target_secs == 92.5);
	assert(audio_command == AUDIO_CMD_PAUSE && playback_status == AUDIO_STATUS_PAUSED);
	assert(progress_current_secs == 92.5);
	// Zero is a new start, never a seek left over from the previous request.
	audio_play_at("/card/music.flac", 0);
	assert(play_request && !seek_request && seek_target_secs == -1.0);
	assert(progress_current_secs == 0 && audio_command == AUDIO_CMD_PLAY);
	audio_play_at("/card/music.flac", 33);
	audio_play_paused("/card/book/chapter.flac", 0);
	assert(play_request && !seek_request && seek_target_secs == -1.0);
	assert(progress_current_secs == 0 && audio_command == AUDIO_CMD_PAUSE);
	// Repeated output failures do not replay the file or reset the cursor.
	reset();
	pause_failed_output();
	pause_failed_output();
	unchanged_source();
	reset();
	play_request = true;
	pause_failed_output();
	assert(play_request && audio_command == AUDIO_CMD_PLAY && playback_status == AUDIO_STATUS_PLAYING);
	unchanged_source();
	reset();
	audio_command = AUDIO_CMD_STOP;
	playback_status = AUDIO_STATUS_STOPPED;
	pause_failed_output();
	assert(audio_command == AUDIO_CMD_STOP && playback_status == AUDIO_STATUS_STOPPED);
	unchanged_source();
	reset();
	stop_thread = true;
	pause_failed_output();
	assert(audio_command == AUDIO_CMD_PLAY);
	reset();
	seek_request = true;
	seek_target_secs = 117.5;
	pause_failed_output();
	assert(seek_request && seek_target_secs == 117.5 && audio_command == AUDIO_CMD_PLAY);
	unchanged_source();
	puts("PASS: failed output preserves source/position and newer play/stop/seek; no file reopen or polling");
	return 0;
}
