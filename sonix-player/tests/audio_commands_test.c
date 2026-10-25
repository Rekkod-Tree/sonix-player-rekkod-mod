// Uses production command/wait code, with pipe-backed ALSA poll descriptors.
// No audio device is opened and no configuration is read or written.
// From sonix-player (native or matching MIPS compiler):
// cc -std=gnu11 -O2 -ffunction-sections -fdata-sections -I. -Ilvgl
//   tests/audio_commands_test.c -Wl,--gc-sections -lasound -pthread -lm -o /tmp/audio_commands_test
#define _GNU_SOURCE
#include "src/system/audio/audio.c"
#include <assert.h>

static int fake_pcm_fd;
static int descriptor_count = 1;
static int fallback_calls;
static int revents_calls;
static bool spurious_ready;

int snd_pcm_poll_descriptors_count(snd_pcm_t *pcm) {
	(void)pcm;
	return descriptor_count;
}
int snd_pcm_poll_descriptors(snd_pcm_t *pcm, struct pollfd *fds, unsigned int space) {
	(void)pcm;
	assert(space >= 1);
	fds[0] = (struct pollfd){.fd = fake_pcm_fd, .events = POLLIN};
	return 1;
}
int snd_pcm_poll_descriptors_revents(snd_pcm_t *pcm, struct pollfd *fds, unsigned int count, unsigned short *events) {
	(void)pcm;
	assert(count == 1);
	revents_calls++;
	*events = spurious_ready ? 0 : (fds[0].revents & POLLIN) ? POLLOUT : fds[0].revents;
	return 0;
}
int snd_pcm_wait(snd_pcm_t *pcm, int timeout) {
	(void)pcm;
	(void)timeout;
	fallback_calls++;
	return 7;
}

static void expect_wake(void) {
	uint64_t value;
	assert(read(playback_wake_fd, &value, sizeof(value)) == sizeof(value) && value > 0);
}

static void *wake_later(void *unused) {
	(void)unused;
	usleep(20 * 1000);
	audio_pause();
	return NULL;
}

int main(void) {
	playback_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	assert(playback_wake_fd >= 0);
	int pipes[2];
	assert(pipe(pipes) == 0);
	fake_pcm_fd = pipes[0];
	snd_pcm_t *pcm = (snd_pcm_t *)&fake_pcm_fd;
	assert(pcm_wait_or_wake(pcm, 5) == 0);
	assert(fallback_calls == 0 && revents_calls == 0);

	// A real command wakes a wait that otherwise has no ready descriptor.
	audio_command = AUDIO_CMD_PLAY;
	playback_status = AUDIO_STATUS_PLAYING;
	pthread_t thread;
	assert(pthread_create(&thread, NULL, wake_later, NULL) == 0);
	long before = log_ms();
	assert(pcm_wait_or_wake(pcm, 1000) == 0);
	assert(pthread_join(thread, NULL) == 0);
	assert(log_ms() - before < 800);
	assert(playback_reader_should_abort());
	assert(playback_status == AUDIO_STATUS_PAUSED);
	uint64_t value;
	assert(read(playback_wake_fd, &value, sizeof(value)) < 0 && errno == EAGAIN);

	// A command wins even when the PCM becomes ready in the same poll.
	assert(write(pipes[1], "x", 1) == 1);
	playback_wake();
	int previous_calls = revents_calls;
	assert(pcm_wait_or_wake(pcm, 100) == 0 && revents_calls == previous_calls);
	assert(pcm_wait_or_wake(pcm, 100) == 1);
	spurious_ready = true;
	before = log_ms();
	assert(pcm_wait_or_wake(pcm, 100) == 0);
	assert(log_ms() - before >= 3);
	spurious_ready = false;
	char byte;
	assert(read(pipes[0], &byte, 1) == 1);

	// Ordinary seek, stop and suspend-restart seek use the real public API.
	seek_request = false;
	restart_fresh_pending = false;
	audio_seek(37.5);
	assert(seek_request && seek_target_secs == 37.5 && progress_current_secs == 37.5);
	expect_wake();
	audio_stop();
	assert(!seek_request && seek_target_secs == -1.0 && playback_reader_should_abort());
	expect_wake();
	strcpy(current_filepath, "/tmp/normal.flac");
	playback_status = AUDIO_STATUS_PAUSED;
	progress_current_secs = 80.0;
	assert(audio_suspend_freeze(0));
	assert(restart_fresh_pending && restart_fresh_pos == 80.0);
	// Freeze also wakes the wait; discard that notification before seeking.
	while (read(playback_wake_fd, &value, sizeof(value)) > 0) {
	}
	audio_seek(12.25);
	assert(restart_fresh_pos == 12.25 && !seek_request);
	assert(progress_current_secs == 12.25);
	expect_wake();
	audio_resume();
	assert(!restart_fresh_pending && play_request && seek_request);
	assert(seek_target_secs == 12.25 && strcmp(current_filepath, "/tmp/normal.flac") == 0);

	// Allocation/descriptor fallback retains the old bounded wait.
	descriptor_count = 9;
	assert(pcm_wait_or_wake(pcm, 3) == 7 && fallback_calls == 1);
	descriptor_count = 1;
	close(playback_wake_fd);
	playback_wake_fd = -1;
	assert(pcm_wait_or_wake(pcm, 3) == 7 && fallback_calls == 2);
	close(pipes[0]);
	close(pipes[1]);
	puts("PASS: PCM wake, simultaneous readiness, pause/stop/seek and fresh resume");
	return 0;
}
