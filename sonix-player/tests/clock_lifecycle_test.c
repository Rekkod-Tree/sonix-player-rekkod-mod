#include "src/system/core/config.h"
// Isolated non-HOST policy fixture. Every clock/RTC/network/process operation
// is intercepted; tzset only affects this test process's conversion rules.
// Build with -I. -pthread and src/system/core/json.c src/system/device/tzrules.c.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef HOST_BUILD
#error Exercise the production clock branch with intercepted operations
#endif
static time_t fake_time(time_t *out);
static int fake_clock_gettime(clockid_t id, struct timespec *out);
static int fake_settimeofday(const struct timeval *tv, const struct timezone *zone);
static int fake_open(const char *path, int flags, ...);
static int fake_close(int fd);
static int fake_ioctl(int fd, unsigned long request, ...);
static int fake_access(const char *path, int mode);
static int fake_system(const char *command);
static int fake_pthread_create(pthread_t *id, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
static int fake_pthread_detach(pthread_t id);
static int fake_pthread_join(pthread_t id, void **result);
#define time fake_time
#define clock_gettime fake_clock_gettime
#define settimeofday fake_settimeofday
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define access fake_access
#define system fake_system
#define pthread_create fake_pthread_create
#define pthread_detach fake_pthread_detach
#define pthread_join fake_pthread_join
#include "src/system/device/clock.c"
#undef time
#undef clock_gettime
#undef settimeofday
#undef open
#undef close
#undef ioctl
#undef access
#undef system
#undef pthread_create
#undef pthread_detach
#undef pthread_join

static time_t wall, boot, ntp_answer;
static int sets, rtc_writes, opens, closes, saves, requests, starts, notices;
static bool set_fails, create_fails, ntp_available;
static void *(*queued_worker)(void *);
static time_t fake_time(time_t *out) {
	if (out)
		*out = wall;
	return wall;
}
static int fake_clock_gettime(clockid_t id, struct timespec *out) {
	assert(id == CLOCK_BOOTTIME || id == CLOCK_MONOTONIC);
	out->tv_sec = boot;
	out->tv_nsec = 0;
	return 0;
}
static int fake_settimeofday(const struct timeval *tv, const struct timezone *zone) {
	(void)zone;
	sets++;
	assert(pthread_mutex_trylock(&ntp_lock) == EBUSY);
	if (set_fails) {
		errno = EPERM;
		return -1;
	}
	wall = tv->tv_sec;
	return 0;
}
static int fake_open(const char *path, int flags, ...) {
	assert(!strcmp(path, "/dev/rtc0"));
	assert(flags == O_WRONLY || flags == O_RDONLY);
	opens++;
	return 71;
}
static int fake_close(int fd) {
	assert(fd == 71);
	closes++;
	return 0;
}
static int fake_ioctl(int fd, unsigned long request, ...) {
	assert(fd == 71 && request == RTC_SET_TIME);
	rtc_writes++;
	return 0;
}
static int fake_access(const char *path, int mode) {
	assert(!strcmp(path, NTPDATE_BIN) && mode == X_OK);
	return ntp_available ? 0 : -1;
}
static int fake_system(const char *command) {
	assert(strstr(command, NTPDATE_BIN " -p 2 -t 2 ") == command);
	requests++;
	wall = ntp_answer;
	return 0;
}
static int fake_pthread_create(pthread_t *id, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
	(void)attr;
	assert(!arg);
	starts++;
	if (create_fails)
		return EAGAIN;
	assert(fn == ntp_worker || fn == zone_worker);
	*id = (pthread_t)1;
	queued_worker = fn;
	return 0;
}
static int fake_pthread_detach(pthread_t id) {
	assert(id == (pthread_t)1);
	return 0;
}
static int fake_pthread_join(pthread_t id, void **out) {
	assert(id == (pthread_t)1);
	(void)out;
	return 0;
}

void config_set(const char *section, const char *key, const char *value) {
	(void)section;
	(void)key;
	(void)value;
}
void config_set_int(const char *section, const char *key, long value) {
	(void)section;
	(void)key;
	(void)value;
}
void config_set_bool(const char *section, const char *key, bool value) {
	(void)section;
	(void)key;
	(void)value;
}
bool config_save(void) {
	saves++;
	return true;
}
const char *stub_config_get(const char *s, const char *k, const char *fallback) {
	(void)s;
	(void)k;
	return fallback;
}
const char *config_get_copy(const char *section, const char *key, const char *fallback, char *out, size_t size) {
 const char *v=stub_config_get(section,key,fallback); if(v && size) snprintf(out,size,"%s",v); return v ? out : NULL;
}

long config_get_int(const char *s, const char *k, long fallback) {
	(void)s;
	(void)k;
	return fallback;
}
bool config_get_bool(const char *s, const char *k, bool fallback) {
	(void)s;
	(void)k;
	return fallback;
}
bool http_get_ex(const char *url, const char *headers, bool errors, char **out, size_t *len, size_t limit, int timeout, int *status) {
	(void)headers;
	(void)errors;
	assert(strstr(url, "https://") == url && limit == 16384 && timeout == 5);
	*out = strdup("{\"timezone\":\"Europe/Rome\"}");
	assert(*out);
	if (len)
		*len = strlen(*out);
	if (status)
		*status = 200;
	return true;
}
static void notified(void) { notices++; }
static int local_hour(void) {
	struct tm tm;
	localtime_r(&wall, &tm);
	return tm.tm_hour;
}
static void reset(void) {
	wall = 1782864000;
	boot = 100;
	ntp_answer = wall;
	sets = rtc_writes = opens = closes = saves = requests = starts = notices = 0;
	set_fails = create_fails = false;
	ntp_available = true;
	queued_worker = NULL;
	ntp_running = false;
	ntp_last_attempt = 0;
	ntp_finished = 0;
	ntp_last_ok = false;
	auto_on = true;
	zone_enabled = false;
	tz_offset_minutes = 180;
	tz_dst = false;
	zone_name[0] = zone_rule[0] = pending_zone[0] = pending_rule[0] = 0;
	clock_set_flag = false;
	ref_wall = ref_boot = 0;
	change_hook = NULL;
	tz_refresh();
}
static void case_zone_toggle(void) {
	reset();
	snprintf(zone_name, sizeof(zone_name), "Europe/Rome");
	snprintf(zone_rule, sizeof(zone_rule), "%s", tzrules_lookup(zone_name));
	assert(local_hour() == 3);
	clock_set_zone_lookup(true);
	assert(local_hour() == 2 && clock_zone_automatic());
	snprintf(pending_zone, sizeof(pending_zone), "%s", zone_name);
	snprintf(pending_rule, sizeof(pending_rule), "%s", zone_rule);
	assert(!clock_poll() && local_hour() == 2);
	clock_set_zone_lookup(false);
	assert(!clock_zone_automatic() && local_hour() == 2);
	assert(clock_utc_offset_minutes() == 60 && clock_dst_enabled());
	clock_set_utc_offset_minutes(240);
	clock_set_dst(false);
	assert(local_hour() == 4);
	clock_set_zone_lookup(true);
	assert(local_hour() == 2);
	clock_set_auto(false);
	assert(!clock_zone_automatic() && local_hour() == 2);
	assert(!sets && !requests && !rtc_writes);
	puts("PASS zone opt-in applies cached rule; disable/manual preserves local time; manual offset remains usable");
}
static void case_pending(void) {
	reset();
	clock_set_change_hook(notified);
	zone_enabled = true;
	zone_worker(NULL);
	assert(notices == 1 && pending_rule[0] && local_hour() == 3);
	assert(clock_poll() && local_hour() == 2 && !pending_rule[0]);
	assert(!clock_poll());
	clock_set_change_hook(NULL);
	zone_worker(NULL);
	assert(notices == 1);
	assert(!clock_poll());
	clock_set_zone_lookup(false);
	clock_set_utc_offset_minutes(300);
	clock_set_dst(false);
	zone_name[0] = zone_rule[0] = 0;
	zone_worker(NULL);
	assert(clock_poll());
	assert(local_hour() == 5 && !clock_zone_automatic());
	puts("PASS worker only publishes; GUI poll applies once; hook-free fallback works; disabled zone cannot override manual offset");
}
static void case_manual(void) {
	reset();
	ntp_running = true;
	auto_on = false;
	assert(!clock_set(2026, 7, 1, 15, 10));
	assert(!sets && !opens && !saves);
	ntp_running = false;
	set_fails = true;
	assert(!clock_set(2026, 7, 1, 15, 10));
	assert(sets == 1 && !opens && !saves);
	set_fails = false;
	assert(clock_set(2026, 7, 1, 15, 10));
	assert(sets == 2 && rtc_writes == 1 && opens == closes && clock_is_set());
	assert(ref_wall == wall && ref_boot == boot && local_hour() == 15);
	puts("PASS busy NTP and rejected settimeofday refuse manual save; successful save holds lock and updates RTC/reference");
}
static void case_network(void) {
	reset();
	auto_on = false;
	assert(!network_sync_start(false) && !starts);
	auto_on = true;
	wall += 86400 * 30;
	assert(network_sync_start(false) && starts == 1);
	assert(network_sync_start(false) && starts == 1);
	clock_set_change_hook(notified);
	assert(queued_worker == ntp_worker);
	queued_worker(NULL);
	assert(!ntp_running && ntp_finished == 1 && requests == 1 && notices == 2);
	assert(wall == ntp_answer && opens == closes);
	boot = 1899;
	assert(!network_sync_start(false) && starts == 1);
	boot = 1900;
	assert(network_sync_start(false) && starts == 2);
	ntp_running = false;
	create_fails = true;
	assert(!network_sync_start(true) && !ntp_running);
	create_fails = false;
	ntp_available = false;
	assert(!network_sync_start(true));
	puts("PASS manual mode blocks automatic admission; single in-flight job; wall-clock rollback does not extend monotonic throttle; thread failure clears busy");
}
int main(void) {
	case_zone_toggle();
	case_pending();
	case_manual();
	case_network();
	puts("PASS clock lifecycle: all clock, RTC, process and HTTP operations intercepted");
	return 0;
}
