// Real failed-initial-open blocks and public commands; run via the Python runner.
#define _GNU_SOURCE
#include AUDIO_IMPLEMENTATION
#include <assert.h>

static const char *opened_path = "/card/book/chapter.m4a";
static int decoder_closes;
static int dop_clears;

void decoder_close(decoder_t *decoder) {
	assert(decoder == (decoder_t *)1);
	decoder_closes++;
}

void set_dac_dop(int enabled) {
	assert(enabled == 0);
	dop_clears++;
}

static void fail_initial_open(void) {
	const char *filepath = opened_path;
#ifdef DECODED_FIXTURE
	decoder_t *dec = (decoder_t *)1;
#else
	FILE *f = tmpfile();
	assert(f);
#endif
	(void)filepath;
#include FAILURE_IMPLEMENTATION
}

static void reset(void) {
	strcpy(current_filepath, opened_path);
	progress_current_secs = 92.5;
	progress_total_secs = 180;
	progress_floor_secs = -1;
	audio_command = AUDIO_CMD_PAUSE;
	playback_status = AUDIO_STATUS_PAUSED;
	play_request = stop_thread = seek_request = restart_fresh_pending = false;
	seek_target_secs = -1;
	track_completed = false;
	pcm_device_open = false;
}

static void expect_retained(double seconds) {
	assert(playback_status == AUDIO_STATUS_PAUSED && restart_fresh_pending);
	assert(!play_request && !seek_request && !track_completed && !pcm_device_open);
	assert(progress_current_secs == seconds && progress_total_secs == 180);
	assert(restart_fresh_pos == seconds && !strcmp(restart_fresh_path, opened_path));
}

int main(void) {
	reset();
	fail_initial_open();
	expect_retained(92.5);
	audio_resume();
	assert(!restart_fresh_pending && play_request && audio_command == AUDIO_CMD_PLAY);
	assert(seek_request && seek_target_secs == 92.5 && !strcmp(current_filepath, opened_path));

	reset();
	audio_seek(117.5);
	fail_initial_open();
	expect_retained(117.5);
	audio_seek(75.25);
	expect_retained(75.25);
	audio_resume();
	assert(play_request && seek_request && seek_target_secs == 75.25);

	reset();
	progress_current_secs = 0;
	fail_initial_open();
	expect_retained(0);
	audio_resume();
	assert(play_request && !seek_request && seek_target_secs == -1);

	reset();
	progress_current_secs = -10;
	fail_initial_open();
	expect_retained(0);

	reset();
	audio_command = AUDIO_CMD_PLAY;
	playback_status = AUDIO_STATUS_PLAYING;
	track_completed = true;
	fail_initial_open();
	expect_retained(92.5);

	// Both a new path and a new request for the same path outrank the old failure.
	for (int same_path = 0; same_path < 2; same_path++) {
		reset();
		audio_play_at(same_path ? opened_path : "/card/music.flac", 15);
		fail_initial_open();
		assert(!restart_fresh_pending && play_request && audio_command == AUDIO_CMD_PLAY);
		assert(playback_status == AUDIO_STATUS_PLAYING && seek_request && seek_target_secs == 15);
	}

	reset();
	strcpy(current_filepath, "/card/replacement.flac");
	fail_initial_open();
	assert(!restart_fresh_pending && !strcmp(current_filepath, "/card/replacement.flac"));

	reset();
	audio_stop();
	fail_initial_open();
	assert(!restart_fresh_pending && playback_status == AUDIO_STATUS_STOPPED && audio_command == AUDIO_CMD_STOP);

	reset();
	stop_thread = true;
	fail_initial_open();
	assert(!restart_fresh_pending && stop_thread);

	// A managed card stop or an explicit new play must cancel the old cold retry.
	reset();
	fail_initial_open();
	audio_stop();
	assert(!restart_fresh_pending && playback_status == AUDIO_STATUS_STOPPED);
	reset();
	fail_initial_open();
	audio_play_at("/card/new.flac", 20);
	assert(!restart_fresh_pending && play_request && seek_target_secs == 20);
#ifdef DECODED_FIXTURE
	assert(decoder_closes == 12 && dop_clears == 12);
#else
	assert(decoder_closes == 0 && dop_clears == 0);
#endif
	puts("PASS: initial-output failure retains position; seek, replacement, stop and shutdown remain authoritative");
	return 0;
}
