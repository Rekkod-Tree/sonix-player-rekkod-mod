// Actual D-Bus protocol on an explicitly private bus, no Bluetooth hardware.
#include "src/system/bluetooth/bapcm.c"
#include "src/system/bluetooth/btstack.c"
#include "src/system/bluetooth/dbuslite.c"
#include <assert.h>

static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_wake = PTHREAD_COND_INITIALIZER;
static unsigned event_count;
static bool wait_for_retirement, saw_retired_invalidation, saw_retired_bluez;
static void changed(void) {
	pthread_mutex_lock(&event_lock);
	bool retiring = wait_for_retirement;
	event_count++;
	pthread_cond_signal(&event_wake);
	pthread_mutex_unlock(&event_lock);
	if (retiring) {
		bool retired = false;
		for (unsigned n = 0; n < 300 && !retired; n++) {
			pthread_mutex_lock(&lock);
			retired = conn == NULL;
			pthread_mutex_unlock(&lock);
			if (!retired)
				usleep(10000);
		}
		assert(retired);
		saw_retired_invalidation = btstack_rebind_needed();
		saw_retired_bluez = btstack_bluez_rebind_needed();
	}
}
static void wait_change(unsigned wanted) {
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 3;
	pthread_mutex_lock(&event_lock);
	while (event_count < wanted) {
		int rc = pthread_cond_timedwait(&event_wake, &event_lock, &deadline);
		assert(rc == 0);
	}
	pthread_mutex_unlock(&event_lock);
}
static void name_call(dbus_conn_t *controller, const char *member, const char *name) {
	bool request = strcmp(member, "RequestName") == 0;
	dbus_writer_t *w = dbus_call_begin(controller, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", member, request ? "su" : "s");
	dbus_w_string(w, name);
	if (request)
		dbus_w_u32(w, 0);
	assert(dbus_call_send(controller, 2000, NULL, 0));
	dbus_reader_t r;
	dbus_reply_reader(controller, &r);
	uint32_t result = 0;
	assert(dbus_r_u32(&r, &result) && result == 1);
}
static void phase(void) {
	assert(btstack_open());
	btstack_set_change_cb(changed);
	pthread_mutex_lock(&event_lock);
	event_count = 0;
	pthread_mutex_unlock(&event_lock);
	pthread_mutex_lock(&lock);
	// Synthetic probe-only endpoints; these never refer to an ALSA device.
	pcm_count = rx_count = transport_count = 1;
	pthread_mutex_unlock(&lock);
}
static void verify_loss(bool bluez, unsigned wanted) {
	wait_change(wanted);
	pthread_mutex_lock(&lock);
	assert(pcm_count == 0 && rx_count == 0 && transport_count == 0);
	pthread_mutex_unlock(&lock);
	assert(!btstack_alive() && btstack_rebind_needed());
	assert(btstack_bluez_rebind_needed() == bluez);
}
int main(int argc, char **argv) {
	static const char prefix[] = "unix:path=/tmp/sonix-bt-owner-";
	assert(argc == 2 && strncmp(argv[1], prefix, sizeof(prefix) - 1) == 0);
	alarm(20);
	assert(setenv("DBUS_SYSTEM_BUS_ADDRESS", argv[1], 1) == 0);
	dbus_conn_t *controller = dbus_connect_system(argv[1], NULL, NULL);
	assert(controller);
	phase();
	name_call(controller, "RequestName", "org.bluez");
	verify_loss(true, 1);
	btstack_close();
	phase();
	name_call(controller, "ReleaseName", "org.bluez");
	verify_loss(true, 1);
	btstack_close();
	phase();
	name_call(controller, "RequestName", "org.bluealsa");
	verify_loss(false, 1);
	assert(btstack_wait_service("org.bluealsa", 250) && "Owner acquisition must not strand the active worker wait");
	assert(btstack_alive());
	btstack_close();
	phase();
	name_call(controller, "ReleaseName", "org.bluealsa");
	verify_loss(false, 1);
	btstack_close();
	phase();
	name_call(controller, "RequestName", "org.bluealsa");
	verify_loss(false, 1);
	name_call(controller, "RequestName", "org.bluez");
	verify_loss(true, 2);
	btstack_refresh_audio(); // Stale connections cannot query the fake service.
	verify_loss(true, 2);
	btstack_close();
	phase();
	name_call(controller, "ReleaseName", "org.bluealsa");
	verify_loss(false, 1);
	pthread_mutex_lock(&lock);
	dbus_conn_t *dying = conn;
	pthread_mutex_unlock(&lock);
	// Only this probe's private Unix socket; no system bus or radio service.
	assert(shutdown(dying->fd, SHUT_RDWR) == 0);
	for (unsigned n = 0; n < 300 && dbus_alive(dying); n++)
		usleep(10000);
	assert(!dbus_alive(dying) && btstack_bluez_rebind_needed());
	btstack_close();
	assert(!conn && btstack_bluez_rebind_needed());
	phase();
	pthread_mutex_lock(&event_lock);
	wait_for_retirement = true;
	pthread_mutex_unlock(&event_lock);
	name_call(controller, "ReleaseName", "org.bluez");
	wait_change(1);
	// close/join races the already-entered reader callback, as in a worker wait.
	btstack_close();
	assert(saw_retired_invalidation && saw_retired_bluez);
	dbus_disconnect(controller);
	alarm(0);
	puts("Private bus owner lifecycle: PASS acquisition/loss/worker-wait/sticky-reason/socket-EOF/retirement-race");
	return 0;
}
