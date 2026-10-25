// Actual shared resume helper and command producers, with a blocked PCM backend.
// No device, configuration, network or card access. Run the Python driver.
#define _GNU_SOURCE
#define reopen_paused_pcm unused_original_reopen_paused_pcm
#include AUDIO_IMPLEMENTATION
#undef reopen_paused_pcm
#include <assert.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool entered, released, fail_open, accepted;
static int closes, route;
static unsigned delay_ms;
static pthread_t owner;
static snd_pcm_t *handle;
static uint64_t last_close_ms;

static void require_settled_close(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t ms = (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
	assert(!last_close_ms || ms - last_close_ms >= 140);
}

int snd_pcm_close(snd_pcm_t *pcm) {
	assert(pthread_equal(pthread_self(), owner));
	assert(pcm == (snd_pcm_t *)1 || pcm == (snd_pcm_t *)2);
	assert(pthread_mutex_trylock(&audio_mutex) == 0);
	pthread_mutex_unlock(&audio_mutex);
	closes++;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	last_close_ms = (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
	return 0;
}

static snd_pcm_t *fixture_open(int channels, int rate, int bits, snd_pcm_uframes_t *period, pcm_open_error_t *error) {
	(void)error;
	assert(channels == 2 && rate == 44100 && bits == 16 && *period == 512);
	assert(pthread_equal(pthread_self(), owner));
	require_settled_close();
	pthread_mutex_lock(&gate);
	entered = true;
	pthread_cond_broadcast(&condition);
	while (!released)
		pthread_cond_wait(&condition, &gate);
	pthread_mutex_unlock(&gate);
	usleep(delay_ms * 1000);
	*period = 1024;
	pcm_device_open = !fail_open;
	return fail_open ? NULL : (snd_pcm_t *)2;
}
static void fixture_route(void) {}
static int fixture_output_key(void) { return route; }
static void fixture_follow(snd_pcm_t *pcm) { assert(!pcm); }
static void fixture_release_due(snd_pcm_t **pcm, uint64_t at) {
	(void)at;
	assert(!*pcm);
	require_settled_close();
	pthread_mutex_lock(&audio_mutex);
	hold_flush_request = false;
	pthread_mutex_unlock(&audio_mutex);
}
#define open_pcm_device fixture_open
#define auto_set_output fixture_route
#define alsa_output_key fixture_output_key
#define playback_follow fixture_follow
#define paused_release_due fixture_release_due
#include RESUME_IMPLEMENTATION
#undef paused_release_due
#undef playback_follow
#undef alsa_output_key
#undef auto_set_output
#undef open_pcm_device

static void *worker(void *unused) {
	(void)unused;
	owner = pthread_self();
	pthread_mutex_lock(&audio_mutex);
	snd_pcm_uframes_t period = 512;
	accepted = reopen_paused_pcm(&handle, 2, 44100, 16, &period, false);
	assert(period == 1024);
	pthread_mutex_unlock(&audio_mutex);
	if (handle) {
		snd_pcm_close(handle);
		handle = NULL;
	}
	return NULL;
}

static void run(int action, unsigned delay, bool old_handle, bool failure) {
	entered = released = accepted = false;
	fail_open = failure;
	delay_ms = delay;
	closes = route = 0;
	last_close_ms = 0;
	handle = old_handle ? (snd_pcm_t *)1 : NULL;
	audio_command = AUDIO_CMD_PLAY;
	playback_status = AUDIO_STATUS_PLAYING;
	play_request = seek_request = stop_thread = false;
	hold_flush_request = resume_recover_pending = false;
	strcpy(current_filepath, "/card/book.m4a");
	strcpy(output_pcm, "default");
	progress_current_secs = 91.25;
	progress_total_secs = 180;
	progress_floor_secs = -1;
	pthread_t thread;
	assert(pthread_create(&thread, NULL, worker, NULL) == 0);
	pthread_mutex_lock(&gate);
	while (!entered)
		pthread_cond_wait(&condition, &gate);
	pthread_mutex_unlock(&gate);

	// This deadline rejects a UI lock held for an arbitrarily slow driver open.
	struct timespec until;
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_nsec += 100000000;
	if (until.tv_nsec >= 1000000000) {
		until.tv_sec++;
		until.tv_nsec -= 1000000000;
	}
	assert(pthread_mutex_timedlock(&audio_mutex, &until) == 0);
	pthread_mutex_unlock(&audio_mutex);
	assert(audio_get_status() == AUDIO_STATUS_PLAYING);
	double position, total;
	audio_get_progress(&position, &total);
	assert(position == 91.25 && total == 180);
	if (action == 1)
		audio_stop();
	if (action == 2)
		audio_seek(120);
	if (action == 3)
		audio_play_at("/card/new.flac", 42);
	if (action == 4)
		audio_play_at("/card/book.m4a", 42);
	if (action == 5)
		audio_pause();
	if (action == 6) {
		audio_pause();
		audio_resume();
	}
	if (action == 7)
		audio_notify_resume();
	if (action == 8)
		route++;
	if (action == 9)
		audio_set_output_device("different-device");
	if (action == 10) {
		pthread_mutex_lock(&audio_mutex);
		hold_flush_request = true;
		pthread_mutex_unlock(&audio_mutex);
	}
	pthread_mutex_lock(&gate);
	released = true;
	pthread_cond_broadcast(&condition);
	pthread_mutex_unlock(&gate);
	assert(pthread_join(thread, NULL) == 0);
	assert(accepted == (!action && !failure));
	assert(closes == (old_handle ? 1 : 0) + (failure ? 0 : 1));
	assert(!handle && !pcm_exact_rate);
	if (action == 1)
		assert(audio_command == AUDIO_CMD_STOP && playback_status == AUDIO_STATUS_STOPPED);
	if (action == 2)
		assert(seek_request && seek_target_secs == 120 && progress_current_secs == 120);
	if (action == 3 || action == 4)
		assert(play_request && seek_request && seek_target_secs == 42);
	if (action == 5)
		assert(audio_command == AUDIO_CMD_PAUSE);
	if (action == 6)
		assert(audio_command == AUDIO_CMD_RESUME);
	if (action == 7)
		assert(resume_recover_pending);
	if (action == 10)
		assert(!hold_flush_request);
	if (!action && failure)
		assert(audio_command == AUDIO_CMD_PAUSE && playback_status == AUDIO_STATUS_PAUSED);
	printf("PASS action=%d delay=%u old=%d failure=%d\n", action, delay, old_handle, failure);
}

int main(void) {
	alarm(20);
	for (int old = 0; old < 2; old++) {
		for (int failure = 0; failure < 2; failure++)
			for (int action = 0; action <= 10; action++)
				run(action, 0, old, failure);
	}
	run(0, 200, true, false);
	run(0, 2000, true, false);
	puts("PASS resume: responsive getters, new commands retained, one owner/close, failed open stays paused");
}
