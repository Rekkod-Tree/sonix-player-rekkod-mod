/*
 * Production USB detection with sysfs/netlink/time fixtures; no device writes.
 * cc -std=gnu11 -O1 -g -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE -I. \
 *   -ffunction-sections -fdata-sections tests/usb_host_test.c \
 *   -Wl,--gc-sections -pthread -o /tmp/usb_host_test && /tmp/usb_host_test
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <linux/netlink.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int test_poll(struct pollfd *, nfds_t, int);
static ssize_t test_recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
static int test_socket(int, int, int);
static int test_bind(int, const struct sockaddr *, socklen_t);
static int test_setsockopt(int, int, int, const void *, socklen_t);
static int test_close(int);
static unsigned test_sleep(unsigned);
static int test_clock_gettime(clockid_t, struct timespec *);
static DIR *test_opendir(const char *);
static FILE *test_fopen(const char *, const char *);
static int test_usleep(useconds_t);
#define poll test_poll
#define recvfrom test_recvfrom
#define socket test_socket
#define bind test_bind
#define setsockopt test_setsockopt
#define close test_close
#define sleep test_sleep
#define clock_gettime test_clock_gettime
#define opendir test_opendir
#define fopen test_fopen
#define usleep test_usleep
#include "src/system/device/usb.c"
#undef poll
#undef recvfrom
#undef socket
#undef bind
#undef setsockopt
#undef close
#undef sleep
#undef clock_gettime
#undef opendir
#undef fopen
#undef usleep

static const char *udc, *gadget, *bound;
static bool fail_socket, fail_bind, poll_error, interrupt_poll;
static unsigned closes, sleeps, polls, bind_port;
static int64_t fake_ms;
struct packet {
	const char *data;
	size_t size;
	unsigned port;
};
static struct packet packets[4];
static size_t packet_count, packet_next;
static bool adb_enabled, adb_running, external_audio, vbus = true;
static bool adb_controller_bound, missing_after_unbind;
static bool missing_lun, bad_lun, fail_reader_bind, change_owner, start_adb, unplug;
static const char *lun = "\n";
static unsigned udc_writes;

bool adb_switched_on(void) { return adb_enabled; }
bool adb_is_running(void) { return adb_running; }
bool usbaudio_active(void) { return external_audio; }

char *read_file_content(const char *path) {
	const char *value = NULL;
	if (!strcmp(path, "/sys/class/udc/13500000.otg_new/state"))
		value = udc;
	else if (!strcmp(path, STORAGE_GADGET "/UDC"))
		value = missing_after_unbind && bound && !bound[0] ? NULL : bound;
	else if (!strcmp(path, ADB_GADGET "/UDC"))
		value = adb_controller_bound ? "13500000.otg_new\n" : NULL;
	else if (!strcmp(path, "/sys/class/usb_gadget/android0/state"))
		value = gadget;
	else if (!strcmp(path, "/sys/class/power_supply/usb/online"))
		value = vbus ? "1\n" : "0\n";
	return value ? strdup(value) : NULL;
}

static ssize_t reader_write(void *unused, const char *data, size_t size) {
	(void)unused;
	udc_writes++;
	if (size == 1 && data[0] == '\n') {
		bound = "";
		if (change_owner)
			owner_serial++;
		if (start_adb)
			adb_enabled = true;
		if (unplug)
			vbus = false;
	} else {
		assert(size == strlen("13500000.otg_new\n") && !memcmp(data, "13500000.otg_new\n", size));
		if (fail_reader_bind) {
			errno = EIO;
			return -1;
		}
		bound = "13500000.otg_new\n";
	}
	return (ssize_t)size;
}

static ssize_t bad_read(void *unused, char *out, size_t size) {
	(void)unused;
	(void)out;
	(void)size;
	errno = EIO;
	return -1;
}

static FILE *test_fopen(const char *path, const char *mode) {
	if (!strcmp(path, STORAGE_GADGET "/UDC")) {
		assert(!strcmp(mode, "w"));
		return fopencookie(NULL, "w", (cookie_io_functions_t){.write = reader_write});
	}
	assert(!strcmp(path, STORAGE_GADGET "/functions/mass_storage.0/lun.0/file") && !strcmp(mode, "r"));
	if (missing_lun) {
		errno = ENOENT;
		return NULL;
	}
	if (bad_lun)
		return fopencookie(NULL, "r", (cookie_io_functions_t){.read = bad_read});
	FILE *file = tmpfile();
	assert(file);
	fputs(lun, file);
	rewind(file);
	return file;
}

static int test_usleep(useconds_t usec) {
	assert(usec == 100000);
	fake_ms += 100;
	return 0;
}

static void reset_reader(void) {
	udc = "default\n";
	gadget = "CONNECTED\n";
	bound = "13500000.otg_new\n";
	adb_enabled = adb_running = external_audio = false;
	adb_controller_bound = missing_after_unbind = false;
	storage_active = dac_owns_usb = false;
	owner_serial = 0;
	vbus = true;
	missing_lun = bad_lun = fail_reader_bind = change_owner = start_adb = unplug = false;
	lun = "\n";
	udc_writes = 0;
}

static void reader_recovery_checks(void) {
	bool spent = false, left = false;
	reset_reader();
	assert(enumeration_started());
	retry_empty_reader(0, &spent, &left);
	assert(spent && !left && udc_writes == 2 && !storage_active);
	retry_empty_reader(0, &spent, &left);
	assert(udc_writes == 2);
	for (int guard = 0; guard < 11; guard++) {
		reset_reader();
		spent = left = false;
		switch (guard) {
		case 0:
			adb_enabled = true;
			break;
		case 1:
			adb_running = true;
			break;
		case 2:
			external_audio = true;
			break;
		case 3:
			dac_owns_usb = true;
			break;
		case 4:
			storage_active = true;
			break;
		case 5:
			owner_serial = 1;
			break;
		case 6:
			lun = "/dev/fixture\n";
			break;
		case 7:
			missing_lun = true;
			break;
		case 8:
			bad_lun = true;
			break;
		case 9:
			adb_controller_bound = true;
			break;
		case 10:
			vbus = false;
			break;
		}
		retry_empty_reader(0, &spent, &left);
		assert(!spent && !left && udc_writes == 0);
	}
	for (int transition = 0; transition < 5; transition++) {
		reset_reader();
		spent = left = false;
		change_owner = transition == 0;
		start_adb = transition == 1;
		unplug = transition == 2;
		fail_reader_bind = transition == 3;
		missing_after_unbind = transition == 4;
		retry_empty_reader(0, &spent, &left);
		assert(spent && left && udc_writes == (transition == 3 ? 2u : 1u));
		unsigned before = udc_writes;
		retry_empty_reader(owner_serial, &spent, &left);
		assert(udc_writes == before);
	}
	reset_reader();
	spent = left = false;
	udc = "configured\n";
	retry_empty_reader(0, &spent, &left);
	assert(!spent && udc_writes == 0);
	udc = "powered\n";
	gadget = "DISCONNECTED\n";
	assert(!enumeration_started()); // A charger cannot even start the retry timer.
	puts("PASS: one empty-reader reset; no card writes; missing/busy LUN, owner, ADB/DAC, unplug and bind failure guards");
}
static DIR *test_opendir(const char *path) {
	assert(!strcmp(path, "/sys/class/udc"));
	return NULL;
}
static int test_socket(int domain, int type, int protocol) {
	assert(domain == AF_NETLINK && protocol == NETLINK_KOBJECT_UEVENT);
	assert((type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) == (SOCK_NONBLOCK | SOCK_CLOEXEC));
	return fail_socket ? -1 : 42;
}
static int test_bind(int fd, const struct sockaddr *address, socklen_t length) {
	assert(fd == 42 && length == sizeof(struct sockaddr_nl));
	const struct sockaddr_nl *nl = (const struct sockaddr_nl *)address;
	assert(nl->nl_family == AF_NETLINK && nl->nl_groups == 1);
	bind_port = nl->nl_pid;
	assert(bind_port && bind_port != (unsigned)getpid());
	return fail_bind ? -1 : 0;
}
static int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
	assert(fd == 42 && level == SOL_SOCKET && option == SO_RCVBUF);
	assert(length == sizeof(int) && *(const int *)value == 16384);
	return 0;
}
static int test_close(int fd) {
	assert(fd == 42);
	closes++;
	return 0;
}
static unsigned test_sleep(unsigned seconds) {
	sleeps += seconds;
	return 0;
}
static int test_clock_gettime(clockid_t clock, struct timespec *out) {
	assert(clock == CLOCK_MONOTONIC);
	out->tv_sec = fake_ms / 1000;
	out->tv_nsec = (fake_ms % 1000) * 1000000;
	return 0;
}
static int test_poll(struct pollfd *ready, nfds_t count, int timeout) {
	assert(count == 1 && ready->fd == 42 && timeout > 0 && timeout <= 4000);
	polls++;
	if (interrupt_poll) {
		interrupt_poll = false;
		fake_ms += 10;
		errno = EINTR;
		return -1;
	}
	if (poll_error) {
		ready->revents = POLLERR;
		return 1;
	}
	if (packet_next < packet_count) {
		ready->revents = POLLIN;
		return 1;
	}
	fake_ms += timeout;
	return 0;
}
static ssize_t test_recvfrom(int fd, void *out, size_t size, int flags, struct sockaddr *sender, socklen_t *length) {
	assert(fd == 42 && flags == MSG_DONTWAIT && *length == sizeof(struct sockaddr_nl));
	if (packet_next == packet_count) {
		errno = EAGAIN;
		return -1;
	}
	struct packet p = packets[packet_next++];
	assert(p.size <= size);
	memcpy(out, p.data, p.size);
	struct sockaddr_nl *nl = (struct sockaddr_nl *)sender;
	nl->nl_family = AF_NETLINK;
	nl->nl_pid = p.port;
	return (ssize_t)p.size;
}

int main(void) {
	udc = "configured\n";
	assert(udc_configured());
	udc = "suspended\n";
	bound = "13500000.otg_new\n";
	gadget = "CONFIGURED\n";
	assert(udc_configured());
	const char *unconfirmed[] = {NULL, "", "CONNECTED\n", "DISCONNECTED\n", "SUSPENDED\n", "CONFIGURED-extra\n"};
	for (size_t i = 0; i < sizeof(unconfirmed) / sizeof(*unconfirmed); i++) {
		gadget = unconfirmed[i];
		assert(!udc_configured());
	}
	gadget = "CONFIGURED\r\n";
	const char *not_bound[] = {NULL, "", "other-controller\n"};
	for (size_t i = 0; i < sizeof(not_bound) / sizeof(*not_bound); i++) {
		bound = not_bound[i];
		assert(!udc_configured());
	}
	bound = "13500000.otg_new\n";
	const char *not_ready[] = {NULL, "", "powered\n", "default\n", "addressed\n", "not attached\n", "unknown\n"};
	for (size_t i = 0; i < sizeof(not_ready) / sizeof(*not_ready); i++) {
		udc = not_ready[i];
		assert(!udc_configured());
	}
	puts("PASS: suspended needs current configuration on the matching gadget; unplug/reset/unknown stay closed");

	const char configured[] = "change@/devices/virtual/usb_gadget/android0\0USB_STATE=CONFIGURED\0";
	const char unrelated[] = "USB_STATE=DISCONNECTED\0";
	assert(host_enumeration_event(configured, sizeof(configured)));
	assert(host_enumeration_event("USB_STATE=CONNECTED", sizeof("USB_STATE=CONNECTED")));
	assert(!host_enumeration_event("USB_STATE=CONFIGURED", 20));
	assert(!host_enumeration_event("xUSB_STATE=CONFIGURED\0", 22));
	assert(!host_enumeration_event("USB_STATE=CONFIGURED-extra\0", 27));
	assert(!host_enumeration_event(unrelated, sizeof(unrelated)));
	assert(!host_enumeration_event("", 0));
	fail_socket = true;
	assert(host_event_socket() < 0 && closes == 0);
	fail_socket = false;
	fail_bind = true;
	assert(host_event_socket() < 0 && closes == 1);
	fail_bind = false;
	assert(host_event_socket() == 42 && bind_port != (unsigned)getpid());
	int fd = 42;
	packets[0] = (struct packet){unrelated, sizeof(unrelated), 0};
	packets[1] = (struct packet){configured, sizeof(configured), 77};
	packet_count = 2;
	host_event_wait(&fd, 1);
	assert(fake_ms == 1000 && packet_next == 2 && fd == 42);
	packets[2] = (struct packet){configured, sizeof(configured), 0};
	packet_count = 3;
	interrupt_poll = true;
	host_event_wait(&fd, 4);
	assert(fake_ms == 1010 && packet_next == 3 && fd == 42);
	// The event wakes promptly, but cannot retain an old configuration.
	udc = "not attached\n";
	assert(!udc_configured());
	poll_error = true;
	host_event_wait(&fd, 1);
	assert(fd == -1 && closes == 2);
	host_event_wait(&fd, 4);
	assert(sleeps == 4);
	puts("PASS: bounded kernel-event wakeup, exact tokens, stale/spoofed events, timeout and polling fallback");
	reader_recovery_checks();
	return 0;
}
