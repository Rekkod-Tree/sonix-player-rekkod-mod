// cc -std=gnu11 -Wall -Wextra -I. tests/sleeptimer_shared_test.c -o /tmp/sleep_test
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "src/system/core/config.h"

static long long clock_ms;
static long shared_on = -1, shared_minutes, legacy_minutes[3], legacy_on[3];
static int writes;
int test_clock_gettime(clockid_t id, struct timespec *ts) {
	(void)id;
	ts->tv_sec = clock_ms / 1000;
	ts->tv_nsec = (clock_ms % 1000) * 1000000;
	return 0;
}
long config_get_int(const char *section, const char *key, long fallback) {
	if (!strcmp(section, "sleep")) return !strcmp(key, "enabled") ? shared_on : shared_minutes;
	const char *names[] = {"player", "audiobook", "podcast"};
	for (int i = 0; i < 3; i++)
		if (!strcmp(section, names[i])) return !strcmp(key, "sleep_timer") ? legacy_on[i] : legacy_minutes[i];
	return fallback;
}
void config_set_int(const char *section, const char *key, long value) {
	assert(!strcmp(section, "sleep"));
	if (!strcmp(key, "enabled")) shared_on = value;
	else shared_minutes = value;
}
bool config_save(void) { writes++; return true; }

#define clock_gettime test_clock_gettime
#include "src/system/playback/sleeptimer.c"
#undef clock_gettime

int main(void) {
	legacy_minutes[1] = 30; legacy_on[1] = 1;
	sleeptimer_init();
	assert(writes == 0 && sleeptimer_enabled(SLEEPTIMER_MUSIC));
	assert(sleeptimer_expiration_serial() == 0);
	assert(sleeptimer_minutes(SLEEPTIMER_PODCAST) == 30);
	sleeptimer_note_playing(SLEEPTIMER_AUDIOBOOK);
	clock_ms += 60000;
	sleeptimer_note_playing(SLEEPTIMER_MUSIC);
	assert(sleeptimer_remaining(SLEEPTIMER_MUSIC) == 29 * 60);
	clock_ms += 60000;
	sleeptimer_note_playing(SLEEPTIMER_COUNT);
	clock_ms += 60000;
	sleeptimer_note_playing(SLEEPTIMER_PODCAST);
	assert(sleeptimer_remaining(SLEEPTIMER_AUDIOBOOK) == 27 * 60);
	for (int i = 0; i < 2000; i++) {
		sleeptimer_note_playing(SLEEPTIMER_PODCAST);
		assert(!sleeptimer_expired(SLEEPTIMER_MUSIC));
	}
	assert(writes == 0);
	clock_ms += 27 * 60000;
	assert(sleeptimer_expired(SLEEPTIMER_AUDIOBOOK));
	assert(sleeptimer_expiration_serial() == 1);
	assert(!sleeptimer_expired(SLEEPTIMER_MUSIC) && writes == 1);
	sleeptimer_note_playing(SLEEPTIMER_MUSIC);
	assert(!sleeptimer_any_running() && !sleeptimer_enabled(SLEEPTIMER_PODCAST));
	sleeptimer_set_enabled(SLEEPTIMER_PODCAST, true);
	assert(sleeptimer_any_running());
	sleeptimer_set_minutes(SLEEPTIMER_AUDIOBOOK, 15);
	assert(sleeptimer_remaining(SLEEPTIMER_MUSIC) == 900);
	clock_ms += 30000;
	int before = writes;
	sleeptimer_start(15);
	assert(sleeptimer_remaining(SLEEPTIMER_MUSIC) == 900 && writes == before);
	sleeptimer_set_enabled(SLEEPTIMER_MUSIC, false);
	assert(!sleeptimer_any_running());
	sleeptimer_set_minutes(SLEEPTIMER_MUSIC, 0);
	sleeptimer_set_enabled(SLEEPTIMER_MUSIC, true);
	assert(!sleeptimer_any_running());
	sleeptimer_set_minutes(SLEEPTIMER_MUSIC, 10000);
	assert(sleeptimer_minutes(SLEEPTIMER_AUDIOBOOK) == 1439);
	sleeptimer_set_enabled(SLEEPTIMER_COUNT, false);
	assert(sleeptimer_enabled(SLEEPTIMER_MUSIC));
	sleeptimer_init();
	assert(sleeptimer_minutes(SLEEPTIMER_MUSIC) == 1439);
	shared_minutes = 0; shared_on = 1;
	sleeptimer_init();
	int zeros = writes;
	sleeptimer_start(0);
	assert(!sleeptimer_enabled(SLEEPTIMER_MUSIC) && shared_on == 0 && writes == zeros + 1);
	shared_minutes = LONG_MAX;
	sleeptimer_init();
	assert(sleeptimer_minutes(SLEEPTIMER_MUSIC) == 1439);
	sleeptimer_set_minutes(SLEEPTIMER_MUSIC, -5);
	assert(sleeptimer_minutes(SLEEPTIMER_PODCAST) == 0 && !sleeptimer_any_running());
	puts("PASS shared sleep: migration, source/pause continuity, no idle writes, one-shot, aliases, persistence, bounds");
}
