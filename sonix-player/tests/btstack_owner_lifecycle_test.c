// Host-only: real D-Bus codecs and btstack, no socket or device.
// cc -std=gnu11 -O1 -g -Wall -Wextra -ffunction-sections -fdata-sections -I.
// tests/btstack_owner_lifecycle_test.c src/system/bluetooth/btrate.c
// -pthread -Wl,--gc-sections -o /tmp/btstack_owner_lifecycle_test
#include "src/system/bluetooth/bapcm.c"
#include "src/system/bluetooth/dbuslite.c"
#include <assert.h>

static void owner_change(const char *name, const char *old_owner, const char *new_owner);
static dbus_conn_t inert_connection = {.fd = -1, .alive = true, .write_lock = PTHREAD_MUTEX_INITIALIZER, .reply_lock = PTHREAD_MUTEX_INITIALIZER, .reply_cond = PTHREAD_COND_INITIALIZER};
static unsigned matches_seen, owner_during_match;
static const char *match_service;
static dbus_writer_t request, response;
static bool lose_during_send, lose_during_parse;
static unsigned reply_reads, default_agent_calls;
static const char *reply_error, *reply_error_member;
static char request_member[64];
static dbus_writer_t *fake_begin(dbus_conn_t *c, const char *dest, const char *path, const char *interface, const char *member, const char *signature) {
	(void)c;
	(void)dest;
	(void)path;
	(void)interface;
	(void)member;
	(void)signature;
	snprintf(request_member, sizeof(request_member), "%s", member);
	memset(&request, 0, sizeof(request));
	return &request;
}
static bool fake_send(dbus_conn_t *c, int timeout, char *error, size_t size) {
	(void)c;
	(void)timeout;
	bool fail = reply_error && (!reply_error_member || !strcmp(reply_error_member, request_member));
	if (!strcmp(request_member, "RequestDefaultAgent"))
		default_agent_calls++;
	if (error && size)
		snprintf(error, size, "%s", fail ? reply_error : "");
	if (lose_during_send) {
		lose_during_send = false;
		owner_change("org.bluez", ":1.10", "");
	}
	return !request.overflow && !fail;
}

static void fake_reader(dbus_conn_t *c, dbus_reader_t *r) {
	(void)c;
	*r = (dbus_reader_t){.data = response.buf, .len = response.used};
	reply_reads++;
	if (lose_during_parse) {
		lose_during_parse = false;
		owner_change("org.bluez", ":1.10", "");
	}
}
static dbus_conn_t *fake_connect(const char *address, dbus_method_cb call, void *user) {
	(void)address;
	(void)call;
	(void)user;
	return &inert_connection;
}
static void fake_disconnect(dbus_conn_t *c) { assert(c == &inert_connection); }
static bool fake_match(dbus_conn_t *c, const char *rule) {
	(void)c;
	matches_seen++;
	if (matches_seen == 1)
		assert(strstr(rule, "arg0='org.bluez'"));
	if (matches_seen == 2)
		assert(strstr(rule, "arg0='org.bluealsa'"));
	if (matches_seen == owner_during_match)
		owner_change(match_service, ":1.10", ":1.11");
	return true;
}
#define dbus_connect_system fake_connect
#define dbus_disconnect fake_disconnect
#define dbus_add_match fake_match
#define dbus_call_begin fake_begin
#define dbus_call_send fake_send
#define dbus_reply_reader fake_reader
#include "src/system/bluetooth/btstack.c"
#undef dbus_call_begin
#undef dbus_call_send
#undef dbus_reply_reader
#undef dbus_connect_system
#undef dbus_disconnect
#undef dbus_add_match

static unsigned changes, cases;
static void changed(void) { changes++; }

static void emit_owner(dbus_conn_t *source, const char *name, const char *old_owner, const char *new_owner, const char *sender, size_t truncate) {
	dbus_writer_t body = {0};
	dbus_w_string(&body, name);
	dbus_w_string(&body, old_owner);
	dbus_w_string(&body, new_owner);
	assert(body.used >= truncate);
	dbus_msg_t m = {.type = DBUS_TYPE_SIGNAL, .body = body.buf, .body_len = body.used - truncate};
	snprintf(m.path, sizeof(m.path), "/org/freedesktop/DBus");
	snprintf(m.interface, sizeof(m.interface), "org.freedesktop.DBus");
	snprintf(m.sender, sizeof(m.sender), "%s", sender);
	snprintf(m.member, sizeof(m.member), "NameOwnerChanged");
	snprintf(m.signature, sizeof(m.signature), "sss");
	on_signal(source, &m, NULL);
}
static void owner_change(const char *name, const char *old_owner, const char *new_owner) { emit_owner(&inert_connection, name, old_owner, new_owner, "org.freedesktop.DBus", 0); }
static void seed(void) {
	// Each scenario starts as an independent live incarnation, with no I/O.
	conn = &inert_connection;
	inert_connection.alive = true;
	service_changed = false;
	bluez_changed = false;
	pcm_count = rx_count = transport_count = headset_battery_count = 1;
	snprintf(pcm_addr[0], sizeof(pcm_addr[0]), "AA:BB:CC:DD:EE:FF");
	snprintf(rx_addr[0], sizeof(rx_addr[0]), "11:22:33:44:55:66");
	snprintf(transports[0].path, sizeof(transports[0].path), "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/sep1/fd0");
	transports[0].sending = true;
	snprintf(media_status, sizeof(media_status), "playing");
	snprintf(media_track.title, sizeof(media_track.title), "old track");
	change_cb = changed;
	changes = reply_reads = 0;
	matches_seen = owner_during_match = 0;
	match_service = NULL;
	default_agent_calls = 0;
	reply_error = reply_error_member = NULL;
	lose_during_send = lose_during_parse = false;
	memset(&response, 0, sizeof(response));
}
static void expect_empty(void) {
	assert(pcm_count == 0 && rx_count == 0 && transport_count == 0 && headset_battery_count == 0);
	assert(!media_status[0] && !media_track.title[0] && service_changed && btstack_rebind_needed());
	assert(!btstack_alive()); // the socket itself is still alive
	cases++;
}
static void pcm_snapshot(void) {
	dbus_array_t objects, interfaces, properties;
	dbus_w_array_begin(&response, "{oa{sa{sv}}}", &objects);
	dbus_w_path(&response, "/org/bluealsa/hci0/dev_AA_BB_CC_DD_EE_FF/a2dpsrc/sink");
	dbus_w_array_begin(&response, "{sa{sv}}", &interfaces);
	dbus_w_string(&response, "org.bluealsa.PCM1");
	dbus_w_array_begin(&response, "{sv}", &properties);
	dbus_w_array_end(&response, &properties);
	dbus_w_array_end(&response, &interfaces);
	dbus_w_array_end(&response, &objects);
}
int main(void) {
	seed();
	owner_change("org.bluez", ":1.10", "");
	expect_empty();
	assert(changes == 1);
	seed();
	owner_change("org.bluealsa", ":1.10", "");
	expect_empty();
	assert(!btstack_bluez_rebind_needed());
	seed();
	owner_change("org.bluez", ":1.10", ":1.11");
	expect_empty();
	seed();
	owner_change("org.bluez", "", ":1.11");
	expect_empty();
	seed();
	owner_change("org.other", ":1.10", "");
	assert(pcm_count == 1 && !changes);
	cases++;
	seed();
	owner_change("org.bluez", ":1.10", ":1.10");
	assert(pcm_count == 1 && !changes);
	cases++;
	seed();
	emit_owner(&inert_connection, "org.bluez", ":1.10", "", ":1.untrusted", 0);
	assert(pcm_count == 1 && !changes);
	cases++;
	seed();
	emit_owner(&inert_connection, "org.bluez", ":1.10", "", "org.freedesktop.DBus", 1);
	assert(pcm_count == 1 && !changes);
	cases++;
	seed();
	dbus_conn_t retired = {0};
	emit_owner(&retired, "org.bluez", ":1.10", "", "org.freedesktop.DBus", 0);
	assert(pcm_count == 1 && !changes);
	cases++;

	seed();
	pcm_snapshot();
	lose_during_send = true;
	btstack_refresh_audio();
	expect_empty();
	assert(reply_reads == 0);
	seed();
	pcm_snapshot();
	lose_during_parse = true;
	btstack_refresh_audio();
	expect_empty();

	seed();
	dbus_w_variant_string(&response, "playing");
	snprintf(media_player_address, sizeof(media_player_address), "AA:BB:CC:DD:EE:FF");
	snprintf(media_player_path, sizeof(media_player_path), "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0");
	lose_during_parse = true;
	btstack_refresh_media_status("AA:BB:CC:DD:EE:FF");
	expect_empty();

	seed();
	dbus_array_t props;
	dbus_w_array_begin(&response, "{sv}", &props);
	dbus_w_dict_string(&response, "Codec", "LDAC");
	dbus_w_dict_variant_begin(&response, "Rate", "u");
	dbus_w_u32(&response, 48000);
	dbus_w_array_end(&response, &props);
	lose_during_parse = true;
	btstack_stream_t info;
	bool valid = btstack_stream_info("AA:BB:CC:DD:EE:FF", false, &info);
	fprintf(stderr, "stale stream info accepted=%d\n", valid);
	assert(!valid);
	expect_empty();
	seed();
	reply_error = "org.bluez.Error.InProgress";
	assert(btstack_discovery(true));
	cases++;
	seed();
	reply_error = "org.bluez.Error.InProgress";
	lose_during_send = true;
	assert(!btstack_discovery(true));
	expect_empty();
	seed();
	reply_error = "org.bluez.Error.DoesNotExist";
	assert(btstack_remove("AA:BB:CC:DD:EE:FF"));
	cases++;
	seed();
	reply_error = "org.bluez.Error.DoesNotExist";
	lose_during_send = true;
	assert(!btstack_remove("AA:BB:CC:DD:EE:FF"));
	expect_empty();
	seed();
	reply_error = "org.bluez.Error.AlreadyExists";
	reply_error_member = "RegisterAgent";
	assert(btstack_register_agent() && default_agent_calls == 1);
	cases++;
	seed();
	reply_error = "org.bluez.Error.AlreadyExists";
	reply_error_member = "RegisterAgent";
	lose_during_send = true;
	assert(!btstack_register_agent() && default_agent_calls == 0);
	expect_empty();
	seed();
	dbus_w_bool(&response, true);
	lose_during_parse = true;
	assert(!btstack_service_ready("org.bluez"));
	expect_empty();
	seed();
	dbus_w_variant_bool(&response, true);
	lose_during_parse = true;
	assert(!btstack_adapter_ready());
	expect_empty();
	seed();
	service_changed = true;
	owner_during_match = 1;
	match_service = "org.bluez";
	assert(!btstack_open() && matches_seen == 1 && btstack_bluez_rebind_needed());
	expect_empty();
	seed();
	service_changed = true;
	owner_during_match = 2;
	match_service = "org.bluealsa";
	assert(!btstack_open() && matches_seen == 2 && !btstack_bluez_rebind_needed());
	expect_empty();
	seed();
	owner_change("org.bluealsa", ":1.10", "");
	inert_connection.alive = false;
	assert(btstack_bluez_rebind_needed());
	cases++;
	btstack_close();
	assert(!conn && btstack_bluez_rebind_needed());
	cases++;
	seed();
	owner_change("org.bluealsa", ":1.10", "");
	btstack_close();
	btstack_close();
	assert(!conn && !btstack_bluez_rebind_needed());
	cases++;
	seed();
	dbus_w_bool(&response, true);
	lose_during_parse = true;
	assert(btstack_wait_service("org.bluez", 200));
	assert(btstack_alive());
	cases++;
	seed();
	dbus_w_variant_bool(&response, true);
	lose_during_parse = true;
	assert(btstack_wait_adapter(300));
	assert(btstack_alive());
	cases++;
	seed();
	btstack_close();
	assert(!btstack_bluez_rebind_needed());
	cases++;
	printf("Bluetooth owner lifecycle: PASS %u cases\n", cases);
	return 0;
}
