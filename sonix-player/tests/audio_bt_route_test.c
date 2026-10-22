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
static int calls, closed_count, live_count, real_stderr = -1;
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
static void release_external_output(void) {release_calls++;}
static void release_page_cache(void) {assert(false && "null backend must not request cache release");}
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
unsigned bluetooth_output_generation(void) {return generation;}
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
	request_t *r = &requests[calls++]; snprintf(r->device, sizeof(r->device), "%s", logical); r->mode = mode; r->slave_rate = slave_rate;
	clock_ms += 13; *out = NULL;
	bool fail = false;
	if (!strcmp(which, "lost-default") || !strcmp(which, "to-b") || !strcmp(which, "to-dac")) {
		if (calls == 3) {
			if (!strcmp(which, "lost-default")) {active = false; audio_set_output_device(NULL);}
			else if (!strcmp(which, "to-b")) {audio_set_output_device(B); generation++; sink_rate = 96000;}
			else {audio_set_output_device(DAC); active = false;}
		}
		fail = !strcmp(logical, A);
	} else if (!strcmp(which, "fallback-race")) {if (calls == 1) return -EINVAL;}
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
	else if (!strcmp(which, "stale-generation") && calls == 1) generation++;
	return result;
}
static int fixture_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
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

int main(int argc, char **argv) {
	assert(argc == 2); which = argv[1];
	bool nonbt = !strcmp(which, "non-bt") || !strcmp(which, "usb-dop");
	pcm_exact_rate = !strcmp(which, "usb-dop") || !strcmp(which, "bt-dop");
	if (!strcmp(which, "inactive")) active = false;
	audio_set_output_device(nonbt ? DAC : A);
	FILE *capture = tmpfile(); assert(capture); int saved = dup(STDERR_FILENO); assert(saved >= 0); real_stderr = saved; assert(dup2(fileno(capture), STDERR_FILENO) >= 0);
	snd_pcm_uframes_t period = 0;
	int rate = pcm_exact_rate ? 176400 : 44100;
	snd_pcm_t *pcm = open_pcm_device_now(2, rate, 32, &period);
	fflush(stderr); assert(dup2(saved, STDERR_FILENO) >= 0 && !close(saved));
	assert(reinit_calls == 1 && release_calls == 1);
	if (!strcmp(which, "churn") || !strcmp(which, "fallback-fails")) {
		assert(!pcm && !live_count && calls == (!strcmp(which, "churn") ? 160 : 161));
	} else {
		assert(pcm && live_count == 1 && period > 0 && pcm_device_open);
		const char *actual = pcm_device_name(pcm);
		const char *expected = (!strcmp(which, "to-b") || !strcmp(which, "cas-race") || !strcmp(which, "late-success") || !strcmp(which, "fallback-race")) ? B : nonbt || !strcmp(which, "to-dac") ? DAC :
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
		if (!strcmp(which, "fallback")) assert(calls == 161 && !strcmp(output_pcm, A));
		if (nonbt || !strcmp(which, "bt-dop")) {assert(calls == 3); for (int i = 0; i < calls; i++) assert(!strcmp(requests[i].device, nonbt ? DAC : A));}
		if (!strcmp(expected, AUDIO_DEFAULT_PCM) || !strcmp(expected, DAC)) assert(!plugin_logs && requests[calls - 1].mode == 0);
		else assert(plugin_logs == (!strcmp(which, "late-success") ? 2u : 1u));
		snd_pcm_hw_params_t *params; snd_pcm_hw_params_alloca(&params); assert(!snd_pcm_hw_params_current(pcm, params));
		unsigned actual_rate, channels; int direction; assert(!snd_pcm_hw_params_get_rate(params, &actual_rate, &direction)); assert(!snd_pcm_hw_params_get_channels(params, &channels));
		assert(actual_rate == (unsigned)rate && channels == 2);
		assert(!fixture_pcm_close(pcm) && !live_count);
	}
	char text[65536]; assert(!fseek(capture, 0, SEEK_SET)); size_t length = fread(text, 1, sizeof(text) - 1, capture); text[length] = 0; assert(!fclose(capture));
	if (!strcmp(which, "lost-default") || !strcmp(which, "fallback")) assert(strstr(text, "PCM open on 'default'"));
	if (!strcmp(which, "grace")) assert(strstr(text, "4 attempts in 202 ms"));
	for (int i = 0; i < calls; i++) printf("TRACE %s slave=%u mode=%d\n", requests[i].device, requests[i].slave_rate, requests[i].mode);
	snd_config_update_free_global();
	printf("PASS %s: %d opens, %d owned closes, %llu observed fixture ms\n", which, calls, closed_count, (unsigned long long)clock_ms);
	return 0;
}
