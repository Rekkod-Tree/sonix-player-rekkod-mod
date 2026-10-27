/*
 * Runs selected production external-PCM functions with deterministic ALSA,
 * clock, and converter doubles. Built by audio_external_recovery_test.py.
 * No live ALSA device or firmware is used.
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif

#define BT_WRITE_STALL_MS 2000
#define EXTERNAL_YIELD_WAIT_MS 1000

typedef struct {
	int id;
	bool closed;
} fake_pcm_t;

typedef struct {
	bool on;
	int32_t *wide;
	int32_t *out;
	int32_t *out2;
} bt_resample_t;

typedef struct {
	int code;
	bool bt_ordinary_endpoint;
} pcm_open_error_t;

typedef struct {
	int32_t *data;
	size_t capacity;
} s24_scratch_t;

static int external_frame_bytes = 4;
static pthread_mutex_t external_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint external_stopping;
static atomic_bool external_wanted;
static snd_pcm_t *external_pcm;
static int external_rate = 44100;
static int external_channels = 2;
static int external_bits = 16;
static int external_buffer_ms = 40;
static int external_dev_rate = 48000;
static int external_dev_bits = 32;
static int external_route;
static char external_device[160] = "fixture";
static long external_checked_ms;
static bool external_yielding;
static unsigned external_underruns;
static long external_underrun_logged_ms;
static unsigned char *external_scaled;
static size_t external_scaled_size;
static bt_resample_t external_conv;
static bool external_dither;
static uint32_t external_dither_seed = 1;
static int32_t *external_narrow;
static size_t external_narrow_samples;
static bool pcm_device_open;
static int pcm_forced_buffer_ms;
static s24_scratch_t external_s24;

static uint64_t fake_now_ms = 100000;
static unsigned write_calls;
static unsigned prepare_calls;
static unsigned resume_calls;
static unsigned open_calls;
static unsigned drop_calls;
static unsigned close_calls;
static unsigned converter_close_calls;
static unsigned begin_open_calls;
static unsigned poll_calls;
static unsigned reopen_after_end_writes;
static unsigned sleep_calls;
static unsigned last_wait_timeout;
static const void *write_ptrs[4096];
static snd_pcm_uframes_t write_sizes[4096];
static fake_pcm_t pcm_first = {1, false};
static fake_pcm_t pcm_reopened = {2, false};
static fake_pcm_t *open_result = &pcm_reopened;
static int write_mode;
static unsigned late_resume_eagains;
static bool prepare_fails;
static bool wait_advances_clock;
static bool converter_mode;
static int32_t converter_output[64];
static bool simulate_waited_end;
static pthread_mutex_t barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_cond = PTHREAD_COND_INITIALIZER;
static bool barrier_waiting;
static bool barrier_release;

enum { WRITE_SUCCESS, WRITE_EAGAIN, WRITE_ZERO, WRITE_EPIPE, WRITE_ESTRPIPE, WRITE_LATE_RESUME, WRITE_PREPARE_FAIL, WRITE_CANCEL_BARRIER, WRITE_CONVERTER_REOPEN, WRITE_CONVERTER_RECOVERS, WRITE_MIXED_STALL, WRITE_PROGRESS_RESETS };

static uint64_t paused_clock_ms(void) { return fake_now_ms; }
static long log_ms(void) { return (long)fake_now_ms; }
const char *snd_strerror(int error) {
	(void)error;
	return "fixture ALSA error";
}
static bool external_follow_output(void) { return true; }
static void gapless_release(void) {}
static bool external_open_locked(void) {
	begin_open_calls++;
	if (!open_result)
		return false;
	external_pcm = (snd_pcm_t *)open_result;
	pcm_device_open = true;
	return true;
}
static bool external_wait_closed(int timeout_ms) {
	assert(timeout_ms == 1000);
	if (simulate_waited_end) {
		external_pcm = NULL;
		pcm_device_open = false;
		atomic_store(&external_stopping, 1);
	}
	return external_pcm == NULL;
}
static bool swvolume_active(void) { return false; }
static bool pcm_takes_s24(snd_pcm_t *pcm, int bits) {
	(void)pcm;
	(void)bits;
	return false;
}
static void dither_to_s16(uint32_t *seed, void *buffer, int samples) {
	(void)seed;
	(void)buffer;
	(void)samples;
}
static void swvolume_apply_u8(unsigned char *buffer, int samples) {
	(void)buffer;
	(void)samples;
}
static void swvolume_apply_s16(short *buffer, int samples) {
	(void)buffer;
	(void)samples;
}
static void swvolume_apply_s24_3le(unsigned char *buffer, int samples) {
	(void)buffer;
	(void)samples;
}
static void swvolume_apply_s32(int32_t *buffer, int samples) {
	(void)buffer;
	(void)samples;
}
static const void *s24_from_s32(s24_scratch_t *scratch, const void *input, size_t samples) {
	(void)scratch;
	(void)samples;
	return input;
}

static const int32_t *bt_resample_block(bt_resample_t *resampler, const void *input, int bits, int frames, int channels, int *made) {
	(void)input;
	(void)bits;
	(void)channels;
	assert(resampler == &external_conv && resampler->on);
	*made = write_mode == WRITE_CONVERTER_RECOVERS ? frames * 2 : frames;
	return resampler->out;
}

static void bt_resample_close(bt_resample_t *resampler) {
	if (resampler == &external_conv)
		converter_close_calls++;
	free(resampler->wide);
	free(resampler->out);
	free(resampler->out2);
	memset(resampler, 0, sizeof(*resampler));
}

static void *pcm_pointer(fake_pcm_t *pcm) { return pcm; }

static snd_pcm_t *open_pcm_device(int channels, int rate, int bits, snd_pcm_uframes_t *period, pcm_open_error_t *error) {
	(void)channels;
	(void)error;
	assert(rate == external_dev_rate && bits == external_dev_bits);
	assert(pcm_forced_buffer_ms == external_buffer_ms);
	open_calls++;
	if (!open_result)
		return NULL;
	assert(!open_result->closed);
	if (period)
		*period = 128;
	return (snd_pcm_t *)pcm_pointer(open_result);
}

int snd_pcm_drop(snd_pcm_t *pcm) {
	assert(pcm == (snd_pcm_t *)&pcm_first || pcm == (snd_pcm_t *)&pcm_reopened);
	drop_calls++;
	return 0;
}

int snd_pcm_close(snd_pcm_t *pcm) {
	fake_pcm_t *fake = (fake_pcm_t *)pcm;
	assert(fake && !fake->closed);
	fake->closed = true;
	close_calls++;
	return 0;
}

static int pcm_wait_bluetooth(snd_pcm_t *pcm, int timeout_ms) {
	assert(pcm && !((fake_pcm_t *)pcm)->closed);
	assert(timeout_ms == 50);
	last_wait_timeout = (unsigned)timeout_ms;
	poll_calls++;
	if (write_mode == WRITE_CANCEL_BARRIER) {
		pthread_mutex_lock(&barrier_lock);
		barrier_waiting = true;
		pthread_cond_broadcast(&barrier_cond);
		while (!barrier_release)
			pthread_cond_wait(&barrier_cond, &barrier_lock);
		pthread_mutex_unlock(&barrier_lock);
		return 0;
	}
	if (wait_advances_clock)
		fake_now_ms += (uint64_t)timeout_ms;
	return 0;
}

int usleep(useconds_t usec) {
	sleep_calls++;
	fake_now_ms += (uint64_t)usec / 1000U;
	return 0;
}

static const void *last_resample_pointer;
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t frames) {
	assert(pcm && !((fake_pcm_t *)pcm)->closed);
	if (write_calls < sizeof(write_ptrs) / sizeof(write_ptrs[0])) {
		write_ptrs[write_calls] = buffer;
		write_sizes[write_calls] = frames;
	}
	write_calls++;
	if (converter_mode) {
		assert(external_conv.on);
		assert(buffer == last_resample_pointer);
		assert(external_conv.out != NULL);
	}
	switch (write_mode) {
	case WRITE_SUCCESS:
		if (write_calls == 1)
			return 2;
		if (write_calls == 2)
			return -EAGAIN;
		return (snd_pcm_sframes_t)frames;
	case WRITE_EAGAIN:
	case WRITE_CONVERTER_REOPEN:
		return -EAGAIN;
	case WRITE_CONVERTER_RECOVERS:
		return open_calls ? (snd_pcm_sframes_t)frames : -EAGAIN;
	case WRITE_ZERO:
		return 0;
	case WRITE_EPIPE:
	case WRITE_PREPARE_FAIL:
		return -EPIPE;
	case WRITE_MIXED_STALL:
		switch (write_calls % 3) {
		case 1:
			return -EPIPE;
		case 2:
			return -ESTRPIPE;
		default:
			return -EAGAIN;
		}
	case WRITE_PROGRESS_RESETS:
		return write_calls <= 190 ? -EPIPE : (write_calls == 191 ? 1 : -EPIPE);
	case WRITE_ESTRPIPE:
		return -ESTRPIPE;
	case WRITE_LATE_RESUME:
		if (write_calls == 1)
			return -ESTRPIPE;
		return (snd_pcm_sframes_t)frames;
	case WRITE_CANCEL_BARRIER:
		return -EAGAIN;
	default:
		assert(!"unknown write mode");
		return -EIO;
	}
}

int snd_pcm_prepare(snd_pcm_t *pcm) {
	assert(pcm && !((fake_pcm_t *)pcm)->closed);
	prepare_calls++;
	return prepare_fails ? -EIO : 0;
}

int snd_pcm_resume(snd_pcm_t *pcm) {
	assert(pcm && !((fake_pcm_t *)pcm)->closed);
	resume_calls++;
	if (write_mode == WRITE_LATE_RESUME)
		return resume_calls <= late_resume_eagains ? -EAGAIN : 0;
	return -EAGAIN;
}

#include EXTERNAL_FUNCTIONS_IMPLEMENTATION

static void reset_fixture(int mode) {
	if (external_pcm)
		audio_external_end();
	write_mode = mode;
	fake_now_ms = 100000;
	write_calls = prepare_calls = resume_calls = open_calls = drop_calls = close_calls = 0;
	converter_close_calls = begin_open_calls = poll_calls = reopen_after_end_writes = sleep_calls = 0;
	last_wait_timeout = 0;
	open_result = &pcm_reopened;
	pcm_first = (fake_pcm_t){1, false};
	pcm_reopened = (fake_pcm_t){2, false};
	prepare_fails = false;
	wait_advances_clock = false;
	converter_mode = false;
	late_resume_eagains = 2;
	barrier_waiting = barrier_release = false;
	simulate_waited_end = false;
	atomic_store(&external_stopping, 0);
	atomic_store(&external_wanted, false);
	external_pcm = (snd_pcm_t *)&pcm_first;
	external_frame_bytes = 4;
	external_rate = 44100;
	external_channels = 2;
	external_bits = 16;
	external_buffer_ms = 40;
	external_dev_rate = 48000;
	external_dev_bits = 32;
	external_route = 0;
	external_yielding = false;
	strcpy(external_device, "fixture");
	external_underruns = 0;
	external_underrun_logged_ms = 0;
	external_scaled = NULL;
	external_scaled_size = 0;
	memset(&external_conv, 0, sizeof(external_conv));
	external_dither = false;
	external_narrow = NULL;
	external_narrow_samples = 0;
	pcm_device_open = true;
	pcm_forced_buffer_ms = 0;
	external_s24 = (s24_scratch_t){0};
	last_resample_pointer = NULL;
}

static void test_partial_write_offsets(void) {
	reset_fixture(WRITE_SUCCESS);
	unsigned char frames[20] = {0};
	assert(audio_external_write(frames, 5) == 5);
	assert(write_calls == 3);
	assert(write_ptrs[0] == frames && write_sizes[0] == 5);
	assert(write_ptrs[1] == frames + 8 && write_sizes[1] == 3);
	assert(write_ptrs[2] == frames + 8 && write_sizes[2] == 3);
	assert(poll_calls == 1 && last_wait_timeout == 50);
}

static void test_stall_reopens_once_without_converter_reset(int mode) {
	reset_fixture(mode);
	wait_advances_clock = true;
	converter_mode = true;
	external_conv.on = true;
	external_conv.out = malloc(sizeof(converter_output));
	assert(external_conv.out);
	memcpy(external_conv.out, converter_output, sizeof(converter_output));
	last_resample_pointer = external_conv.out;
	int16_t frames[8] = {0};
	assert(audio_external_write(frames, 4) == (mode == WRITE_CONVERTER_RECOVERS ? 4 : 0));
	assert(open_calls == 1 && close_calls == 1 && drop_calls == 1);
	assert(pcm_reopened.id == 2 && !pcm_reopened.closed);
	assert(external_conv.on && external_conv.out == last_resample_pointer);
	assert(converter_close_calls == 0);
	assert(write_calls == 42);
	assert(write_ptrs[write_calls - 1] == last_resample_pointer);
	assert(poll_calls == 40 && last_wait_timeout == 50);
	assert(external_dev_rate == 48000 && external_dev_bits == 32);
	/* Teardown owns and finally releases the live converter allocation. */
	audio_external_end();
	assert(converter_close_calls == 1 && !external_conv.on);
}

static void test_prepare_refusal_is_terminal(void) {
	reset_fixture(WRITE_PREPARE_FAIL);
	prepare_fails = true;
	char frames[16] = {0};
	assert(audio_external_write(frames, 4) == 0);
	assert(write_calls == 1 && prepare_calls == 1);
	assert(open_calls == 0 && poll_calls == 0);
}

static void test_late_resume_recovers(void) {
	reset_fixture(WRITE_LATE_RESUME);
	char frames[16] = {0};
	assert(audio_external_write(frames, 4) == 4);
	assert(resume_calls == 3 && write_calls == 2);
	assert(prepare_calls == 0 && open_calls == 0);
}

static void test_begin_refuses_after_waited_teardown(void) {
	reset_fixture(WRITE_SUCCESS);
	external_yielding = true;
	simulate_waited_end = true;
	assert(!audio_external_begin_latency(44100, 2, 16, 40));
	assert(begin_open_calls == 0 && open_calls == 0);
	assert(external_pcm == NULL && !pcm_device_open);
	/* The simulated end owner completes its counter release after the wait. */
	atomic_store(&external_stopping, 0);
}

static void test_repeated_recovery_modes_are_bounded(void) {
	const int modes[] = {WRITE_EAGAIN, WRITE_ZERO, WRITE_EPIPE, WRITE_ESTRPIPE};
	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
		reset_fixture(modes[i]);
		wait_advances_clock = true;
		char frames[16] = {0};
		assert(audio_external_write(frames, 4) == 0);
		assert(open_calls == 1);
		assert(write_calls > 1 && write_calls < 210);
		assert(close_calls == 1 && drop_calls == 1);
		if (modes[i] == WRITE_EPIPE)
			assert(prepare_calls > 1);
		if (modes[i] == WRITE_ESTRPIPE)
			assert(resume_calls > 1 && sleep_calls >= 90);
	}
}

static void test_all_errors_share_one_no_progress_window(void) {
	reset_fixture(WRITE_MIXED_STALL);
	wait_advances_clock = true;
	uint64_t start = fake_now_ms;
	char frames[16] = {0};
	assert(audio_external_write(frames, 4) == 0);
	assert(open_calls == 1 && write_calls < 100);
	assert(fake_now_ms - start >= BT_WRITE_STALL_MS);
	assert(fake_now_ms - start < BT_WRITE_STALL_MS + 100);
}

static void test_positive_progress_resets_deadline(void) {
	reset_fixture(WRITE_PROGRESS_RESETS);
	char frames[16] = {0};
	uint64_t start = fake_now_ms;
	assert(audio_external_write(frames, 4) == 1);
	assert(write_calls >= 380 && write_calls < 410);
	assert(open_calls == 1);
	assert(fake_now_ms - start >= 3800 && fake_now_ms - start < 4100);
}

static void *external_writer(void *unused) {
	(void)unused;
	char frames[16] = {0};
	assert(audio_external_write(frames, 4) == 0);
	return NULL;
}

static void *external_ender(void *unused) {
	(void)unused;
	audio_external_end();
	return NULL;
}

static void test_end_cancels_writer_before_teardown(void) {
	reset_fixture(WRITE_CANCEL_BARRIER);
	pthread_t writer, ender;
	assert(pthread_create(&writer, NULL, external_writer, NULL) == 0);
	pthread_mutex_lock(&barrier_lock);
	while (!barrier_waiting)
		pthread_cond_wait(&barrier_cond, &barrier_lock);
	pthread_mutex_unlock(&barrier_lock);
	assert(pthread_create(&ender, NULL, external_ender, NULL) == 0);
	for (unsigned i = 0; i < 1000000 && !external_write_cancelled(); i++)
		sched_yield();
	assert(external_write_cancelled());
	pthread_mutex_lock(&barrier_lock);
	barrier_release = true;
	pthread_cond_broadcast(&barrier_cond);
	pthread_mutex_unlock(&barrier_lock);
	assert(pthread_join(writer, NULL) == 0);
	assert(pthread_join(ender, NULL) == 0);
	assert(write_calls == 1 && close_calls == 1 && drop_calls == 1);
	assert(external_pcm == NULL && !pcm_device_open);
	assert(!external_write_cancelled());
	assert(reopen_after_end_writes == 0);
}

int main(void) {
	test_partial_write_offsets();
	test_prepare_refusal_is_terminal();
	test_late_resume_recovers();
	test_begin_refuses_after_waited_teardown();
	test_repeated_recovery_modes_are_bounded();
	test_all_errors_share_one_no_progress_window();
	test_positive_progress_resets_deadline();
	test_stall_reopens_once_without_converter_reset(WRITE_CONVERTER_REOPEN);
	test_stall_reopens_once_without_converter_reset(WRITE_CONVERTER_RECOVERS);
	test_end_cancels_writer_before_teardown();
	puts("PASS external PCM recovery: partial offsets, bounded EAGAIN/zero/EPIPE/ESTRPIPE, late resume, prepare refusal, converter-safe single reopen, end cancellation");
	return 0;
}
