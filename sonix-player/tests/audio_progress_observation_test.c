// cc -O1 -std=gnu11 -ffunction-sections -fdata-sections -I. -Ilvgl
// tests/audio_progress_observation_test.c -Wl,--gc-sections -pthread -lm -o /tmp/audio_progress_test
#define _GNU_SOURCE
#include "src/system/audio/audio.c"
#include <assert.h>
int main(void) {
	double position, total;
	unsigned serial;
	progress_current_secs = 9;
	progress_total_secs = 30;
	progress_floor_secs = 15;
	progress_serial = progress_reported_serial = 7;
	audio_get_progress(&position, &total);
	assert(position == 15);
	assert(audio_get_listening_progress(&position, &total, &serial));
	assert(position == 9 && total == 30 && serial == 7);
	play_request = true;
	progress_serial++;
	report_position(99);
	assert(!audio_get_listening_progress(&position, &total, &serial) && position == 9);
	play_request = false;
	assert(!audio_get_listening_progress(NULL, NULL, NULL));
	report_position(10);
	assert(audio_get_listening_progress(&position, NULL, &serial) && position == 10 && serial == 8);
	seek_request = true;
	progress_serial++;
	progress_current_secs = 20;
	report_position(10.1);
	assert(!audio_get_listening_progress(&position, NULL, NULL) && position == 20);
	seek_request = false;
	assert(!audio_get_listening_progress(NULL, NULL, NULL));
	report_position(20.1);
	assert(audio_get_listening_progress(&position, NULL, &serial) && position == 20.1 && serial == 9);
	puts("PASS actual progress ignores UI floor; play/seek wait for a new PCM report; stale blocks do not acknowledge new transport");
	return 0;
}
