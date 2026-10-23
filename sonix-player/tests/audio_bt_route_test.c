// Actual open/setup + real ALSA null/config parser. No hardware or bus access.
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "src/system/audio/audio.h"

#include "actual_route_globals.inc"
static const char *A = "bluealsa:DEV=AA:BB:CC:DD:EE:FF,PROFILE=a2dp";
static const char *B = "bluealsa:DEV=11:22:33:44:55:66,PROFILE=a2dp";
static const char *DAC = "hw:2,0";
static const char *which;
static uint64_t clock_ms;
static bool active = true, cas_injected;
static unsigned generation, sink_rate = 48000, plugin_logs, reinit_calls, release_calls, wakes;
static int calls, params_calls, cache_releases, expired_refreshes, interleave_phase, closed_count, live_count, real_stderr = -1;
static pthread_mutex_t interleave_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t interleave_cond = PTHREAD_COND_INITIALIZER;
static bool interleave_entered, interleave_release;
typedef struct { char device[160]; unsigned slave_rate; int mode; } request_t;
static request_t requests[162];
typedef struct { snd_pcm_t *pcm; bool live; char name[160]; } handle_t;
static handle_t handles[162];
static int handle_count;

static int fixture_clock_gettime(clockid_t id, struct timespec *out) {
	assert(id == CLOCK_MONOTONIC); out->tv_sec = (time_t)(clock_ms / 1000); out->tv_nsec = (long)(clock_ms % 1000) * 1000000; return 0;
}
static int fixture_usleep(useconds_t usec) {clock_ms += usec / 1000; return 0;}
static void playback_wake(void) {wakes++;}
static void alsa_wait_output_reinit(void) {reinit_calls++;}
static void release_external_output(void) {
	release_calls++;
	if (!strcmp(which, "interleaved-errors") && interleave_phase == 1) {
		pthread_mutex_lock(&interleave_lock); interleave_entered = true; pthread_cond_broadcast(&interleave_cond);
		while (!interleave_release) pthread_cond_wait(&interleave_cond, &interleave_lock);
		pthread_mutex_unlock(&interleave_lock);
	}
}
static void release_page_cache(void) {assert(!strcmp(which, "hw-nomem")); cache_releases++;}
static void log_bluealsa_plugin(void) {plugin_logs++;}
static void bt_seek_probe_cancel(void) {}
static void log_pcm_chain(snd_pcm_t *pcm) {(void)pcm;}
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section; (void)key; return fallback;
}
bool bluetooth_audio_active(void) {
	if (!strcmp(which, "cas-race") && !cas_injected) {
		cas_injected = true; audio_set_output_device(B); active = true; return false;
	}
	return active;
}
unsigned bluetooth_output_generation(void) {
	if ((!strcmp(which, "deadline-churn") || !strcmp(which, "deadline-fallback")) && clock_ms >= 8000) {
		if (expired_refreshes >= 7 && real_stderr >= 0) assert(dup2(real_stderr, STDERR_FILENO) >= 0);
		assert(expired_refreshes++ < 8 && "expired route refresh must not spin"); generation++;
	}
	return generation;
}
unsigned bluetooth_sink_rate(void) {return active ? sink_rate : 0;}

static handle_t *find_handle(snd_pcm_t *pcm) {
	for (int i = handle_count - 1; i >= 0; i--) if (handles[i].pcm == pcm && handles[i].live) return &handles[i];
	assert(false && "handle is not owned/live"); return NULL;
}
static const char *fixture_pcm_name(snd_pcm_t *pcm) {return pcm ? find_handle(pcm)->name : NULL;}
static int fixture_pcm_close(snd_pcm_t *pcm) {
	handle_t *h = find_handle(pcm); h->live = false; live_count--; closed_count++; return snd_pcm_close(pcm);
}
static int fixture_open(snd_pcm_t **out, const char *name, const char *logical, unsigned slave_rate, int mode) {
	assert(pthread_mutex_trylock(&output_lock) == 0); assert(!pthread_mutex_unlock(&output_lock));
	if (calls >= 161 && real_stderr >= 0) assert(dup2(real_stderr, STDERR_FILENO) >= 0);
	assert(calls < 161 && "retry budget must remain finite across route churn");
	if (live_count && real_stderr >= 0) assert(dup2(real_stderr, STDERR_FILENO) >= 0);
	assert(live_count == 0 && "every failed attempt must be closed before the next open");
	request_t *r = &requests[calls++]; snprintf(r->device, sizeof(r->device), "%s", logical); r->mode = mode; r->slave_rate = slave_rate;
	clock_ms += 13; *out = NULL;
	bool fail = false;
	if (!strcmp(which, "hw-lost-default") || !strcmp(which, "hw-lost-default-eagain")) {
		if (calls == 1) {active = false; audio_set_output_device(NULL); fail = true;}
	} else if (!strcmp(which, "deadline-churn")) fail = true;
	else if (!strcmp(which, "hw-open-budget")) fail = calls <= 80;
	else if (!strcmp(which, "lost-default") || !strcmp(which, "to-b") || !strcmp(which, "to-dac")) {
		if (calls == 3) {
			if (!strcmp(which, "lost-default")) {active = false; audio_set_output_device(NULL);}
			else if (!strcmp(which, "to-b")) {audio_set_output_device(B); generation++; sink_rate = 96000;}
			else {audio_set_output_device(DAC); active = false;}
		}
		fail = !strcmp(logical, A);
	} else if (!strcmp(which, "fallback-race") || !strcmp(which, "deadline-fallback")) {if (calls == 1) return -EINVAL;}
	else if (!strcmp(which, "grace")) fail = calls <= 3;
	else if (!strcmp(which, "generation")) {if (calls == 1) generation++; fail = calls == 1;}
	else if (!strcmp(which, "churn")) {audio_set_output_device(calls % 2 ? B : A); generation++; fail = true;}
	else if (!strcmp(which, "fallback") || !strcmp(which, "fallback-fails")) fail = strcmp(logical, AUDIO_DEFAULT_PCM) != 0 || !strcmp(which, "fallback-fails");
	else if (!strcmp(which, "non-bt") || !strcmp(which, "usb-dop")) {if (calls == 1) audio_set_output_device(B); fail = calls <= 2;}
	else if (!strcmp(which, "bt-dop")) {if (calls == 1) audio_set_output_device(DAC); fail = calls <= 2;}
	if (fail) return !strncmp(logical, "bluealsa", 8) ? -ENODEV : -EBUSY;
	int result = snd_pcm_open(out, "null", SND_PCM_STREAM_PLAYBACK, mode);
	assert(result >= 0 && *out && handle_count < 162);
	handle_t *h = &handles[handle_count++]; h->pcm = *out; h->live = true; snprintf(h->name, sizeof(h->name), "%s", name); live_count++;
	if (!strcmp(which, "stale-success") || !strcmp(which, "converted-reuse")) {
		if (calls == 1) {audio_set_output_device(NULL); active = false;}
		if (calls > 1 && !strcmp(which, "converted-reuse")) {
			// Force the exact identity state produced if the allocator reuses
			// a closed converted handle address for this real null-backed PCM.
			bt_converted_pcm = *out; snprintf(bt_converted_device, sizeof(bt_converted_device), "%s", A);
		}
	} else if (!strcmp(which, "fallback-race") && calls == 2) {audio_set_output_device(B); generation++; sink_rate = 96000;}
	else if (!strcmp(which, "deadline-fallback") && calls == 2) {clock_ms = 8100; audio_set_output_device(B); generation++;}
	else if (!strcmp(which, "stale-generation") && calls == 1) generation++;
	return result;
}
static int fixture_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
	params_calls++;
	if (!strcmp(which, "hw-lost-default")) return -EBUSY;
	if (!strcmp(which, "hw-lost-default-eagain")) return -EAGAIN;
	if (!strcmp(which, "interleaved-errors")) return interleave_phase == 0 ? -EBUSY : -EINVAL;
	if ((!strcmp(which, "hw-grace") && params_calls <= 3) || !strcmp(which, "hw-never") ||
		!strcmp(which, "hw-open-budget") || !strcmp(which, "hw-non-bt") || !strcmp(which, "hw-dop")) return -EBUSY;
	if (!strcmp(which, "hw-eagain") && params_calls <= 2) return -EAGAIN;
	if (!strcmp(which, "hw-generation") && params_calls == 1) {
		audio_set_output_device(B); generation++; sink_rate = 96000; return -EBUSY;
	}
	if (!strcmp(which, "hw-hard-format")) return -EINVAL;
	if (!strcmp(which, "hw-nomem") && params_calls <= 2) return -ENOMEM;
	int result = snd_pcm_hw_params(pcm, params);
	if (!strcmp(which, "late-success") && calls == 1) {audio_set_output_device(B); generation++; sink_rate = 96000;}
	return result;
}
static int fixture_pcm_open(snd_pcm_t **out, const char *name, snd_pcm_stream_t stream, int mode) {
	assert(stream == SND_PCM_STREAM_PLAYBACK); return fixture_open(out, name, name, 0, mode);
}
static int fixture_pcm_open_lconf(snd_pcm_t **out, const char *name, snd_pcm_stream_t stream, int mode, snd_config_t *conf) {
	assert(stream == SND_PCM_STREAM_PLAYBACK && !strcmp(name, BT_CONVERTED_PCM));
	snd_config_t *node; const char *logical; long rate;
	assert(!snd_config_search(conf, "pcm." BT_CONVERTED_PCM ".slave.pcm", &node)); assert(!snd_config_get_string(node, &logical));
	assert(!snd_config_search(conf, "pcm." BT_CONVERTED_PCM ".slave.rate", &node)); assert(!snd_config_get_integer(node, &rate));
	return fixture_open(out, name, logical, (unsigned)rate, mode);
}

#define clock_gettime fixture_clock_gettime
#define usleep fixture_usleep
#define snd_pcm_name fixture_pcm_name
#define snd_pcm_close fixture_pcm_close
#define snd_pcm_open fixture_pcm_open
#define snd_pcm_open_lconf fixture_pcm_open_lconf
#define snd_pcm_hw_params fixture_hw_params
#include "actual_route_functions.inc"
#undef clock_gettime
#undef usleep
#undef snd_pcm_name
#undef snd_pcm_close
#undef snd_pcm_open
#undef snd_pcm_open_lconf
#undef snd_pcm_hw_params

static void check_format_policy(void) {
#ifdef HAS_FORMAT_CLASSIFIER
	snd_pcm_t *pcm_handle = NULL, *external_pcm = NULL;
	int dev_bits = 32;
	bool passthrough = false;
	struct {bool on;} resampler = {true}, external_conv = {true};
	pcm_open_error_t open_error = {0};
	for (int bt = 0; bt <= 1; bt++) {
		open_error.bt_ordinary_endpoint = bt;
		int errors[] = {-EBUSY, -EAGAIN};
		for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
			open_error.code = errors[i];
			assert(!!ACTUAL_S16_FALLBACK == !bt && !!ACTUAL_RESAMPLED_FALLBACK == !bt && !!ACTUAL_EXTERNAL_FALLBACK == !bt);
		}
		open_error.code = -ENOMEM;
		assert(!ACTUAL_S16_FALLBACK && !ACTUAL_RESAMPLED_FALLBACK && !ACTUAL_EXTERNAL_FALLBACK);
		open_error.code = -EINVAL;
		assert(ACTUAL_S16_FALLBACK && ACTUAL_RESAMPLED_FALLBACK && ACTUAL_EXTERNAL_FALLBACK);
		open_error.code = -EIO;
		assert(ACTUAL_S16_FALLBACK && ACTUAL_RESAMPLED_FALLBACK && ACTUAL_EXTERNAL_FALLBACK);
	}
	passthrough = true; assert(!ACTUAL_S16_FALLBACK);
#endif
}
#ifdef HAS_PCM_ERROR_CONTEXT
static void *local_open_thread(void *arg) {
	snd_pcm_uframes_t period = 0;
	assert(!open_pcm_device(2, 44100, 32, &period, arg));
	return NULL;
}
static int check_interleaved_errors(void) {
	pcm_open_error_t external_failure = {0}, local_failure = {0};
	snd_pcm_uframes_t period = 0;
	assert(!open_pcm_device(2, 44100, 32, &period, &external_failure));
	assert(external_failure.code == -EBUSY && external_failure.bt_ordinary_endpoint);
	interleave_phase = 1;
	pthread_t thread; assert(!pthread_create(&thread, NULL, local_open_thread, &local_failure));
	pthread_mutex_lock(&interleave_lock);
	while (!interleave_entered) pthread_cond_wait(&interleave_cond, &interleave_lock);
	// Local entry has reached the exact release_external_output barrier that
	// serialized hardware ownership but did not protect the old global error.
	assert(external_failure.code == -EBUSY && !pcm_setup_fallback_allowed(&external_failure));
	interleave_phase = 2; interleave_release = true; pthread_cond_broadcast(&interleave_cond); pthread_mutex_unlock(&interleave_lock);
	assert(!pthread_join(thread, NULL));
	assert(local_failure.code == -EINVAL && pcm_setup_fallback_allowed(&local_failure));
	assert(external_failure.code == -EBUSY && !pcm_setup_fallback_allowed(&external_failure));
	assert(!live_count && closed_count == params_calls);
	snd_config_update_free_global();
	puts("PASS interleaved-errors: actual local/external contexts stay distinct across release barrier");
	return 0;
}
#else
static void *local_open_thread(void *arg) {
	(void)arg;
	snd_pcm_uframes_t period = 0;
	assert(!open_pcm_device(2, 44100, 32, &period));
	return NULL;
}
static int check_interleaved_errors(void) {
	snd_pcm_uframes_t period = 0;
	assert(!open_pcm_device(2, 44100, 32, &period));
	assert(last_open_err == -EBUSY);
	interleave_phase = 1;
	pthread_t thread; assert(!pthread_create(&thread, NULL, local_open_thread, NULL));
	pthread_mutex_lock(&interleave_lock);
	while (!interleave_entered) pthread_cond_wait(&interleave_cond, &interleave_lock);
	// This actual legacy-wrapper reset loses the external caller's failure.
	assert(last_open_err == -EBUSY && "local open must not overwrite external setup error");
	interleave_phase = 2; interleave_release = true; pthread_cond_broadcast(&interleave_cond); pthread_mutex_unlock(&interleave_lock);
	assert(!pthread_join(thread, NULL));
	return 0;
}
#endif

int main(int argc, char **argv) {
	assert(argc == 2); which = argv[1]; check_format_policy();
	bool nonbt = !strcmp(which, "non-bt") || !strcmp(which, "usb-dop") || !strcmp(which, "hw-non-bt");
	pcm_exact_rate = !strcmp(which, "usb-dop") || !strcmp(which, "bt-dop") || !strcmp(which, "hw-dop");
	if (!strcmp(which, "inactive")) active = false;
	audio_set_output_device(nonbt ? DAC : A);
	if (!strcmp(which, "interleaved-errors")) return check_interleaved_errors();
	FILE *capture = tmpfile(); assert(capture); int saved = dup(STDERR_FILENO); assert(saved >= 0); real_stderr = saved; assert(dup2(fileno(capture), STDERR_FILENO) >= 0);
	snd_pcm_uframes_t period = 0;
	int rate = pcm_exact_rate ? 176400 : 44100;
	#ifdef HAS_PCM_ERROR_CONTEXT
	pcm_open_error_t failure = {0};
	snd_pcm_t *pcm = open_pcm_device_now(2, rate, 32, &period, &failure);
#else
	snd_pcm_t *pcm = open_pcm_device_now(2, rate, 32, &period);
#endif
	fflush(stderr); assert(dup2(saved, STDERR_FILENO) >= 0 && !close(saved));
	assert(reinit_calls == 1 && release_calls == 1);
	if (!strcmp(which, "deadline-fallback")) {
		assert(!pcm && !live_count && calls == 2 && closed_count == 1 && expired_refreshes == 1);
	} else if (!strcmp(which, "hw-lost-default") || !strcmp(which, "hw-lost-default-eagain")) {
		assert(!pcm && !live_count && calls == 2 && closed_count == 1 && params_calls == 1);
		assert(!strcmp(requests[0].device, A) && !strcmp(requests[1].device, AUDIO_DEFAULT_PCM));
#ifdef HAS_PCM_ERROR_CONTEXT
		assert(!failure.bt_ordinary_endpoint && pcm_setup_fallback_allowed(&failure));
#endif
	} else if (!strcmp(which, "hw-never") || !strcmp(which, "hw-open-budget") || !strcmp(which, "hw-non-bt") || !strcmp(which, "hw-dop") || !strcmp(which, "hw-hard-format")) {
		assert(!pcm && !live_count && closed_count == params_calls);
		if (!strcmp(which, "hw-never") || !strcmp(which, "hw-open-budget")) {
			assert(calls > 1 && calls <= 160 && clock_ms >= 8000 && clock_ms <= 8013);
			for (int i = 0; i < calls; i++) assert(!strcmp(requests[i].device, A));
			#ifdef HAS_PCM_ERROR_CONTEXT
			assert(failure.code == -EBUSY);
#endif
		} else assert(calls == 1 && closed_count == 1);
	} else if (!strcmp(which, "churn") || !strcmp(which, "fallback-fails") || !strcmp(which, "deadline-churn")) {
		assert(!pcm && !live_count && calls <= 161 && calls > 1);
	} else {
		assert(pcm && live_count == 1 && period > 0 && pcm_device_open);
		const char *actual = pcm_device_name(pcm);
		const char *expected = (!strcmp(which, "to-b") || !strcmp(which, "cas-race") || !strcmp(which, "late-success") || !strcmp(which, "fallback-race") || !strcmp(which, "hw-generation")) ? B : nonbt || !strcmp(which, "to-dac") ? DAC :
			(!strcmp(which, "lost-default") || !strcmp(which, "stale-success") || !strcmp(which, "converted-reuse") || !strcmp(which, "inactive") || !strcmp(which, "fallback")) ? AUDIO_DEFAULT_PCM : A;
		assert(!strcmp(actual, expected));
		if (!strcmp(which, "lost-default") || !strcmp(which, "to-b") || !strcmp(which, "to-dac")) {
			assert(calls == 4); for (int i = 0; i < 3; i++) assert(!strcmp(requests[i].device, A)); assert(!strcmp(requests[3].device, expected));
		}
		if (!strcmp(which, "to-b")) assert(requests[0].slave_rate == 48000 && requests[3].slave_rate == 96000 && requests[3].mode == SND_PCM_NONBLOCK);
		if (!strcmp(which, "stale-success") || !strcmp(which, "converted-reuse") || !strcmp(which, "stale-generation") || !strcmp(which, "late-success")) assert(calls == 2 && closed_count == 1 && clock_ms >= 176);
		if (!strcmp(which, "fallback-race")) assert(calls == 3 && closed_count == 1 && !strcmp(output_pcm, B));
		if (!strcmp(which, "generation")) assert(calls == 2 && !strcmp(requests[1].device, A));
		if (!strcmp(which, "grace")) assert(calls == 4 && active && !strcmp(output_pcm, A));
		if (!strcmp(which, "cas-race")) assert(calls == 1 && !strcmp(output_pcm, B));
		if (!strcmp(which, "fallback")) assert(calls <= 161 && calls > 1 && !strcmp(output_pcm, A));
		if (nonbt || !strcmp(which, "bt-dop")) {assert(calls == 3); for (int i = 0; i < calls; i++) assert(!strcmp(requests[i].device, nonbt ? DAC : A));}
		if (!strcmp(expected, AUDIO_DEFAULT_PCM) || !strcmp(expected, DAC)) assert(!plugin_logs && requests[calls - 1].mode == 0);
		else assert(plugin_logs == (!strcmp(which, "late-success") ? 2u : 1u));
		if (!strcmp(which, "hw-grace")) assert(calls == 4 && closed_count == 3 && params_calls == 4);
		if (!strcmp(which, "hw-eagain")) assert(calls == 3 && closed_count == 2 && params_calls == 3);
		if (!strcmp(which, "hw-generation")) assert(calls == 2 && closed_count == 1 && requests[1].slave_rate == 96000);
		if (!strcmp(which, "hw-nomem")) assert(calls == 1 && params_calls == 3 && cache_releases == 1);
		snd_pcm_hw_params_t *params; snd_pcm_hw_params_alloca(&params); assert(!snd_pcm_hw_params_current(pcm, params));
		unsigned actual_rate, channels; int direction; assert(!snd_pcm_hw_params_get_rate(params, &actual_rate, &direction)); assert(!snd_pcm_hw_params_get_channels(params, &channels));
		assert(actual_rate == (unsigned)rate && channels == 2);
		assert(!fixture_pcm_close(pcm) && !live_count);
	}
	char text[65536]; assert(!fseek(capture, 0, SEEK_SET)); size_t length = fread(text, 1, sizeof(text) - 1, capture); text[length] = 0; assert(!fclose(capture));
	if (!strcmp(which, "lost-default") || !strcmp(which, "fallback")) assert(strstr(text, "PCM open on 'default'"));
	if (!strcmp(which, "grace")) assert(strstr(text, "4 attempts in 202 ms"));
	if (!strcmp(which, "churn") || !strcmp(which, "deadline-churn")) {char needle[64]; snprintf(needle, sizeof(needle), "after %d attempts", calls); assert(strstr(text, needle));}
	for (int i = 0; i < calls; i++) printf("TRACE %s slave=%u mode=%d\n", requests[i].device, requests[i].slave_rate, requests[i].mode);
	snd_config_update_free_global();
	printf("PASS %s: %d opens, %d owned closes, %llu observed fixture ms\n", which, calls, closed_count, (unsigned long long)clock_ms);
	return 0;
}
