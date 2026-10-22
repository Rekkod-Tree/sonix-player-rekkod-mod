/* Actual private pause helpers and public command producers; inert ALSA backend.
 * Built by audio_pause_wait_test.py against production audio.c; no live device.
 */
#define _GNU_SOURCE
#define audio_external_begin_yielding unused_original_external_begin_yielding
#include AUDIO_IMPLEMENTATION
#undef audio_external_begin_yielding
#include <assert.h>
#include <limits.h>

static bool backend_external_result;
static unsigned backend_external_calls;
static bool fixture_external_begin_latency(int rate, int channels, int bits, int buffer_ms);
static void fixture_release_external_output(void);
static bool fixture_external_wait_closed(int timeout_ms);
#define audio_external_begin_latency fixture_external_begin_latency
#define release_external_output fixture_release_external_output
#define external_wait_closed fixture_external_wait_closed
#include PAUSE_PRODUCER_IMPLEMENTATION
#undef external_wait_closed
#undef release_external_output
#undef audio_external_begin_latency

int __real_poll(struct pollfd *, nfds_t, int);
int __real_clock_gettime(clockid_t, struct timespec *);
int __real_usleep(useconds_t);
ssize_t __real_read(int, void *, size_t);

enum { POLL_NORMAL, POLL_BARRIER, POLL_TIMEOUT, POLL_EINTR_ONCE, POLL_ERROR, POLL_BAD_BITS };
static atomic_int poll_mode;
static atomic_uint poll_calls, sleep_calls, last_sleep;
static atomic_int last_timeout;
static atomic_bool fake_clock, fake_sleep, wait_done;
static atomic_bool fail_read;
static atomic_ullong fake_now_ms;
static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_cond = PTHREAD_COND_INITIALIZER;
static bool poll_entered, release_poll;
static bool worker_local;
static uint64_t worker_started;

static void wait_poll_entered(void) {
	struct timespec until;
	__real_clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += 2;
	pthread_mutex_lock(&fixture_lock);
	while (!poll_entered)
		assert(pthread_cond_timedwait(&fixture_cond, &fixture_lock, &until) == 0);
	pthread_mutex_unlock(&fixture_lock);
}
static void allow_poll(void) {
	pthread_mutex_lock(&fixture_lock);
	release_poll = true;
	pthread_cond_broadcast(&fixture_cond);
	pthread_mutex_unlock(&fixture_lock);
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *now) {
	if (clock == CLOCK_MONOTONIC && atomic_load(&fake_clock)) {
		uint64_t ms = atomic_load(&fake_now_ms);
		now->tv_sec = (time_t)(ms / 1000U);
		now->tv_nsec = (long)(ms % 1000U) * 1000000L;
		return 0;
	}
	return __real_clock_gettime(clock, now);
}
int __wrap_usleep(useconds_t us) {
	if (atomic_load(&fake_sleep)) {
		atomic_fetch_add(&sleep_calls, 1);
		atomic_store(&last_sleep, us);
		return 0;
	}
	return __real_usleep(us);
}
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout) {
	assert(count == 1 && fds[0].fd == playback_wake_fd);
	atomic_fetch_add(&poll_calls, 1);
	atomic_store(&last_timeout, timeout);
	int mode = atomic_load(&poll_mode);
	if (mode == POLL_BARRIER) {
		pthread_mutex_lock(&fixture_lock);
		poll_entered = true;
		pthread_cond_broadcast(&fixture_cond);
		while (!release_poll)
			pthread_cond_wait(&fixture_cond, &fixture_lock);
		pthread_mutex_unlock(&fixture_lock);
	}
	if (mode == POLL_TIMEOUT) {
		assert(timeout >= 0);
		atomic_fetch_add(&fake_now_ms, (unsigned)timeout);
		return 0;
	}
	if (mode == POLL_EINTR_ONCE) {
		atomic_store(&poll_mode, POLL_NORMAL);
		errno = EINTR;
		return -1;
	}
	if (mode == POLL_ERROR) {
		errno = EIO;
		return -1;
	}
	if (mode == POLL_BAD_BITS) {
		fds[0].revents = POLLNVAL;
		return 1;
	}
	return __real_poll(fds, count, timeout);
}
int __wrap___poll_chk(struct pollfd *fds, nfds_t count, int timeout, size_t bytes) {
	assert(bytes >= count * sizeof(*fds));
	return __wrap_poll(fds, count, timeout);
}
ssize_t __wrap_read(int fd, void *buffer, size_t bytes) {
	if (fd == playback_wake_fd && atomic_load(&fail_read)) {
		errno = EIO;
		return -1;
	}
	return __real_read(fd, buffer, bytes);
}
ssize_t __wrap___read_chk(int fd, void *buffer, size_t bytes, size_t capacity) {
	assert(bytes <= capacity);
	return __wrap_read(fd, buffer, bytes);
}

typedef struct {
	int id;
	const char *name;
	bool closed;
} fake_pcm_t;
static unsigned close_calls;
static bool close_requires_flush;
static void (*close_hook)(fake_pcm_t *);
static snd_pcm_t *local_pcm;
static fake_pcm_t external_fixture = {99, "fixture-external", false};

int snd_pcm_close(snd_pcm_t *pcm) {
	fake_pcm_t *fake = (fake_pcm_t *)pcm;
	assert(fake && !fake->closed);
	if (close_requires_flush)
		assert(hold_flush_request);
	if (close_hook)
		close_hook(fake);
	fake->closed = true;
	close_calls++;
	return 0;
}
const char *snd_pcm_name(snd_pcm_t *pcm) { return ((fake_pcm_t *)pcm)->name; }
snd_pcm_state_t snd_pcm_state(snd_pcm_t *pcm) { (void)pcm; return SND_PCM_STATE_SETUP; }
int snd_pcm_drain(snd_pcm_t *pcm) { (void)pcm; return 0; }
int snd_pcm_start(snd_pcm_t *pcm) { (void)pcm; return 0; }
int snd_pcm_drop(snd_pcm_t *pcm) { (void)pcm; return 0; }
int snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delay) { (void)pcm; *delay = 0; return 0; }
static bool fixture_external_begin_latency(int rate, int channels, int bits, int buffer_ms) {
	assert(rate == 44100 && channels == 2 && bits == 16 && buffer_ms == 40);
	assert(!local_pcm);
	backend_external_calls++;
	if (backend_external_result) {
		external_pcm = (snd_pcm_t *)&external_fixture;
		pcm_device_open = true;
	}
	return backend_external_result;
}
static void fixture_release_external_output(void) { assert(!"No prior external owner in this fixture"); }
static bool fixture_external_wait_closed(int timeout_ms) { (void)timeout_ms; assert(!"No prior external owner"); return false; }

static void drain_wake(void) {
	uint64_t value;
	while (read(playback_wake_fd, &value, sizeof(value)) > 0) {}
	assert(errno == EAGAIN);
}
static void reset(void) {
	if (playback_wake_fd >= 0)
		close(playback_wake_fd);
	playback_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	assert(playback_wake_fd >= 0);
	pthread_mutex_lock(&audio_mutex);
	audio_command = AUDIO_CMD_PAUSE;
	playback_status = AUDIO_STATUS_PAUSED;
	play_request = seek_request = stop_thread = resume_recover_pending = hold_flush_request = false;
	restart_fresh_pending = false;
	progress_current_secs = 123.25;
	progress_total_secs = 900;
	progress_floor_secs = -1;
	track_completed = false;
	playback_context_active = true;
	pthread_mutex_unlock(&audio_mutex);
	atomic_store(&paused_release_wanted, false);
	held_pcm = NULL;
	external_pcm = NULL;
	external_yielding = false;
	pcm_device_open = false;
	local_pcm = NULL;
	close_calls = 0;
	close_requires_flush = false;
	close_hook = NULL;
	backend_external_calls = 0;
	backend_external_result = false;
	playback_rt = false;
	follow_pcm = NULL;
	pthread_mutex_lock(&output_lock);
	snprintf(output_pcm, sizeof(output_pcm), "hw:fixture");
	pthread_mutex_unlock(&output_lock);
	atomic_store(&poll_mode, POLL_NORMAL);
	atomic_store(&poll_calls, 0);
	atomic_store(&sleep_calls, 0);
	atomic_store(&last_sleep, 0);
	atomic_store(&wait_done, false);
	atomic_store(&fake_clock, true);
	atomic_store(&fake_sleep, false);
	atomic_store(&fail_read, false);
	atomic_store(&fake_now_ms, 100000);
	poll_entered = release_poll = false;
	worker_started = paused_clock_ms();
}
static void *wait_once(void *unused) {
	(void)unused;
	paused_wait(worker_local, worker_started);
	atomic_store(&wait_done, true);
	return NULL;
}
static void *wait_then_service(void *unused) {
	(void)unused;
	paused_wait(local_pcm != NULL, worker_started);
	paused_release_due(&local_pcm, worker_started);
	atomic_store(&wait_done, true);
	return NULL;
}
static pthread_t begin_wait(void *(*entry)(void *), bool local) {
	worker_local = local;
	worker_started = paused_clock_ms();
	atomic_store(&poll_mode, POLL_BARRIER);
	pthread_t thread;
	assert(pthread_create(&thread, NULL, entry, NULL) == 0);
	wait_poll_entered();
	return thread;
}
static void joined(pthread_t thread) {
	assert(pthread_join(thread, NULL) == 0);
	assert(atomic_load(&wait_done));
}
static void quiesce_during_close(fake_pcm_t *fake) {
	(void)fake;
	close_hook = NULL;
	assert(pcm_device_open && playback_context_active);
	assert(!audio_suspend_quiesce(0));
	assert(!audio_suspend_quiesce(0));
	assert(hold_flush_request && pcm_device_open);
}
static void expect_next_pause_delay(void) {
	hold_flush_request = false; // Callers separately assert consumed acknowledgement.
	audio_resume();
	audio_pause();
	fake_pcm_t next = {71, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&next;
	pcm_device_open = true;
	uint64_t started = paused_clock_ms();
	paused_release_due(&local_pcm, started);
	assert(local_pcm && !next.closed);
	atomic_fetch_add(&fake_now_ms, 4999);
	paused_release_due(&local_pcm, started);
	assert(local_pcm);
	atomic_fetch_add(&fake_now_ms, 1);
	paused_release_due(&local_pcm, started);
	assert(!local_pcm && next.closed && playback_context_active);
}

int main(void) {
	/* Monotonic 64-bit deadline remains correct beyond a 32-bit ms uptime. */
	reset();
	atomic_store(&fake_now_ms, (uint64_t)UINT32_MAX + 1000000U);
	uint64_t start = paused_clock_ms();
	assert(start > UINT32_MAX);
	fake_pcm_t dac = {1, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&dac; pcm_device_open = true;
	paused_release_due(&local_pcm, start);
	assert(local_pcm);
	atomic_fetch_add(&fake_now_ms, 4999);
	atomic_store(&poll_mode, POLL_TIMEOUT);
	paused_wait(true, start);
	assert(atomic_load(&last_timeout) == 1);
	paused_release_due(&local_pcm, start);
	assert(!local_pcm && dac.closed && close_calls == 1);
	assert(playback_context_active && !track_completed && progress_current_secs == 123.25);
	paused_release_due(&local_pcm, start); assert(close_calls == 1);

	/* Stale wakes and EINTR do not restart the original five-second clock. */
	reset(); fake_pcm_t bounded = {8, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&bounded; pcm_device_open = true; start = paused_clock_ms();
	atomic_fetch_add(&fake_now_ms, 4000); playback_wake(); paused_wait(true, start);
	assert(atomic_load(&last_timeout) == 1000 && local_pcm);
	atomic_fetch_add(&fake_now_ms, 900); atomic_store(&poll_mode, POLL_EINTR_ONCE);
	paused_wait(true, start); assert(atomic_load(&last_timeout) == 100);
	atomic_fetch_add(&fake_now_ms, 99); atomic_store(&poll_mode, POLL_TIMEOUT);
	paused_wait(true, start); assert(atomic_load(&last_timeout) == 1);
	paused_release_due(&local_pcm, start); assert(!local_pcm && close_calls == 1);

	/* Bluetooth does not inherit the DAC's five-second hold. */
	reset(); snprintf(output_pcm, sizeof(output_pcm), "bluealsa:fixture");
	fake_pcm_t bt = {2, "bluealsa:fixture", false};
	local_pcm = (snd_pcm_t *)&bt; pcm_device_open = true;
	paused_release_due(&local_pcm, paused_clock_ms());
	assert(!local_pcm && bt.closed && close_calls == 1);

	/* Resume before predicate and between predicate-unlock and poll. */
	reset(); audio_resume(); paused_wait(false, paused_clock_ms());
	assert(!atomic_load(&poll_calls) && audio_command == AUDIO_CMD_RESUME);
	reset();
	pthread_t thread = begin_wait(wait_once, false);
	audio_resume(); allow_poll(); joined(thread);
	assert(audio_command == AUDIO_CMD_RESUME && playback_status == AUDIO_STATUS_PLAYING);
	uint64_t count;
	assert(read(playback_wake_fd, &count, sizeof(count)) < 0 && errno == EAGAIN);

	/* A closed pause stays in one real poll until an actual command arrives. */
	reset(); thread = begin_wait(wait_once, false);
	allow_poll(); __real_usleep(80000);
	assert(!atomic_load(&wait_done) && atomic_load(&poll_calls) == 1 && atomic_load(&last_timeout) == -1);
	audio_stop(); joined(thread);
	assert(audio_command == AUDIO_CMD_STOP && !track_completed);

	reset(); thread = begin_wait(wait_once, false);
	audio_resume(); audio_stop(); allow_poll(); joined(thread);
	assert(audio_command == AUDIO_CMD_STOP && !track_completed);
	reset(); thread = begin_wait(wait_once, false);
	audio_resume(); audio_pause(); allow_poll(); joined(thread);
	assert(audio_command == AUDIO_CMD_PAUSE && playback_status == AUDIO_STATUS_PAUSED && !track_completed);
	reset(); thread = begin_wait(wait_once, false);
	audio_seek(77.5); allow_poll(); joined(thread);
	assert(seek_request && progress_current_secs == 77.5 && audio_command == AUDIO_CMD_PAUSE);
	reset(); thread = begin_wait(wait_once, false);
	audio_play_at("/tmp/fixture-book.m4a", 90.0); allow_poll(); joined(thread);
	assert(play_request && seek_request && seek_target_secs == 90.0 && !track_completed);
	reset(); thread = begin_wait(wait_once, false);
	audio_play("/tmp/fixture.flac"); allow_poll(); joined(thread);
	assert(play_request && audio_command == AUDIO_CMD_PLAY && !track_completed);
	reset(); thread = begin_wait(wait_once, false);
	audio_play_paused("/tmp/fixture-book.m4a", 60.0); allow_poll(); joined(thread);
	assert(play_request && seek_request && audio_command == AUDIO_CMD_PAUSE && progress_current_secs == 60.0);
	reset(); thread = begin_wait(wait_once, false);
	assert(!audio_suspend_freeze(0)); allow_poll(); joined(thread);
	assert(audio_command == AUDIO_CMD_STOP && playback_context_active && !restart_fresh_pending);

	/* Recovery and real output changes have their own notifications. */
	reset(); thread = begin_wait(wait_once, false);
	audio_notify_resume(); allow_poll(); joined(thread); assert(resume_recover_pending);
	reset(); thread = begin_wait(wait_once, false);
	audio_set_output_device("bluealsa:new-fixture"); allow_poll(); joined(thread);
	audio_set_output_device("bluealsa:new-fixture");
	assert(read(playback_wake_fd, &count, sizeof(count)) < 0 && errno == EAGAIN);

	/* Quiesce closes owned local PCM early without claiming context death. */
	reset(); fake_pcm_t local = {3, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&local; pcm_device_open = true;
	close_requires_flush = true;
	thread = begin_wait(wait_then_service, true);
	allow_poll();
	assert(audio_suspend_quiesce(1000)); joined(thread);
	assert(local.closed && !local_pcm && !hold_flush_request && playback_context_active);
	close_requires_flush = false;
	expect_next_pause_delay();

	/* Quiesce at local NULL still services a live held-gapless PCM. */
	reset(); fake_pcm_t held = {4, "hw:fixture", false};
	held_pcm = (snd_pcm_t *)&held; pcm_device_open = true;
	close_requires_flush = true;
	thread = begin_wait(wait_then_service, false); allow_poll();
	assert(audio_suspend_quiesce(1000)); joined(thread);
	assert(held.closed && !held_pcm && !hold_flush_request && playback_context_active);
	close_requires_flush = false;
	expect_next_pause_delay();

	/* Already closed requests are consumed once instead of poisoning next pause. */
	reset(); assert(audio_suspend_quiesce(0)); assert(hold_flush_request);
	paused_wait(false, paused_clock_ms());
	assert(!atomic_load(&poll_calls));
	paused_release_due(&local_pcm, paused_clock_ms()); assert(!hold_flush_request);
	assert(audio_suspend_quiesce(0)); assert(hold_flush_request);
	paused_wait(false, paused_clock_ms()); paused_release_due(&local_pcm, paused_clock_ms());
	assert(!hold_flush_request && close_calls == 0);
	expect_next_pause_delay();

	/* Requests during a real close are not acknowledged before close returns. */
	reset(); fake_pcm_t closing = {5, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&closing; pcm_device_open = true;
	close_hook = quiesce_during_close;
	close_requires_flush = true;
	hold_flush_request = true;
	paused_release_due(&local_pcm, paused_clock_ms());
	assert(closing.closed && !hold_flush_request && close_calls == 1 && playback_context_active);
	close_requires_flush = false;
	expect_next_pause_delay();

	/* The real external-yield producer is extracted unchanged; opening is inert. */
	reset(); fake_pcm_t yielding = {6, "hw:fixture", false};
	local_pcm = (snd_pcm_t *)&yielding; pcm_device_open = true;
	thread = begin_wait(wait_then_service, true); allow_poll();
	backend_external_result = true;
	assert(audio_external_begin_yielding(44100, 2, 16, 40)); joined(thread);
	assert(yielding.closed && backend_external_calls == 1 && external_yielding);
	assert(!atomic_load(&paused_release_wanted) && external_pcm == (snd_pcm_t *)&external_fixture);
	assert(playback_context_active && !external_fixture.closed);
	/* LOCAL NULL must ignore an external request still owned by its producer. */
	atomic_store(&paused_release_wanted, true); drain_wake();
	poll_entered = release_poll = false; atomic_store(&wait_done, false);
	thread = begin_wait(wait_once, false); audio_stop(); allow_poll(); joined(thread);
	assert(!external_fixture.closed);

	/* Fatal fd/poll failures retain the old bounded waits, not a spin. */
	reset(); close(playback_wake_fd); playback_wake_fd = -1;
	atomic_store(&fake_sleep, true);
	paused_wait(false, paused_clock_ms()); assert(atomic_load(&last_sleep) == 200000);
	paused_wait(true, paused_clock_ms()); assert(atomic_load(&last_sleep) == 50000);
	reset(); atomic_store(&fake_sleep, true); atomic_store(&poll_mode, POLL_ERROR);
	paused_wait(false, paused_clock_ms()); paused_wait(false, paused_clock_ms());
	assert(atomic_load(&sleep_calls) == 2 && atomic_load(&poll_calls) == 2);
	atomic_store(&poll_mode, POLL_BAD_BITS);
	paused_wait(true, paused_clock_ms()); assert(atomic_load(&last_sleep) == 50000);
	reset(); playback_wake(); atomic_store(&fake_sleep, true); atomic_store(&fail_read, true);
	paused_wait(false, paused_clock_ms()); paused_wait(false, paused_clock_ms());
	assert(atomic_load(&sleep_calls) == 2 && atomic_load(&last_sleep) == 200000);
	atomic_store(&fail_read, false); drain_wake();
	reset(); atomic_store(&poll_mode, POLL_EINTR_ONCE);
	paused_wait(true, paused_clock_ms()); assert(!atomic_load(&sleep_calls));

	/* A stale event causes one turn, not recurring work or a changed command. */
	reset(); playback_wake(); paused_wait(false, paused_clock_ms());
	assert(audio_command == AUDIO_CMD_PAUSE && atomic_load(&poll_calls) == 1);
	poll_entered = release_poll = false; atomic_store(&wait_done, false);
	thread = begin_wait(wait_once, false); audio_pause(); allow_poll(); joined(thread);
	assert(audio_command == AUDIO_CMD_PAUSE);
	reset(); stop_thread = true; paused_wait(false, paused_clock_ms());
	assert(!atomic_load(&poll_calls));
	close(playback_wake_fd); playback_wake_fd = -1;
	puts("PASS pause13: real eventfd/lost-wake, monotonic64 deadlines, producers, quiesce acknowledgement, external ownership and bounded failure waits");
	return 0;
}
