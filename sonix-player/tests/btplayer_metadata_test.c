// From sonix-player, add -I/path/to/the/existing/LVGL/build-tree if needed:
// cc -O1 -g -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
// -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE=1 -I. tests/btplayer_metadata_test.c
// -pthread -Wl,--gc-sections -o /tmp/btplayer_metadata_test
// Add -fsanitize=address,undefined -fno-omit-frame-pointer for sanitizer checks.
#include "src/system/bluetooth/dbuslite.c"
#include "src/system/playback/device_state.h"

#include <assert.h>

static device_state_t fake_state;
static dbus_writer_t wire;
static unsigned signals, registrations, unregisters, disconnects;

void device_state_get(device_state_t *out) { *out = fake_state; }
audio_status_t audio_get_status(void) { return fake_state.status; }

static dbus_writer_t *fake_signal_begin(dbus_conn_t *c, const char *path, const char *interface,
									  const char *member, const char *signature) {
	assert(c && !strcmp(path, "/org/mpris/MediaPlayer2"));
	assert(!strcmp(interface, "org.freedesktop.DBus.Properties"));
	assert(!strcmp(member, "PropertiesChanged") && !strcmp(signature, "sa{sv}as"));
	memset(&wire, 0, sizeof(wire));
	return &wire;
}

static bool fake_signal_send(dbus_conn_t *c) {
	assert(c && !wire.overflow);
	signals++;
	return true;
}

static dbus_writer_t *fake_call_begin(dbus_conn_t *c, const char *destination, const char *path,
									const char *interface, const char *member, const char *signature) {
	assert(c && !strcmp(destination, "org.bluez") && !strcmp(path, "/org/bluez/hci0"));
	assert(!strcmp(interface, "org.bluez.Media1"));
	if (!strcmp(member, "RegisterPlayer")) {
		assert(!strcmp(signature, "oa{sv}"));
		registrations++;
	} else {
		assert(!strcmp(member, "UnregisterPlayer") && !strcmp(signature, "o"));
		unregisters++;
	}
	memset(&wire, 0, sizeof(wire));
	return &wire;
}

static bool fake_call_send(dbus_conn_t *c, int timeout_ms, char *error, size_t size) {
	assert(c && timeout_ms > 0 && !wire.overflow);
	if (error && size)
		error[0] = '\0';
	return true;
}

static bool fake_alive(const dbus_conn_t *c) { return c != NULL; }
static void fake_disconnect(dbus_conn_t *c) {
	assert(c);
	disconnects++;
}

#define dbus_signal_begin fake_signal_begin
#define dbus_signal_send fake_signal_send
#define dbus_call_begin fake_call_begin
#define dbus_call_send fake_call_send
#define dbus_alive fake_alive
#define dbus_disconnect fake_disconnect
#include "src/system/bluetooth/btplayer.c"
#undef dbus_signal_begin
#undef dbus_signal_send
#undef dbus_call_begin
#undef dbus_call_send
#undef dbus_alive
#undef dbus_disconnect

// Decode the real production Metadata signal, not a separate test dictionary.
static void check_wire(const char *title, const char *artist, const char *album, int64_t length_us) {
	dbus_reader_t r = {.data = wire.buf, .len = wire.used};
	char text[512], signature[32];
	assert(dbus_r_string(&r, text, sizeof(text)) && !strcmp(text, PLAYER_IFACE));
	dbus_array_iter_t changed, metadata, artists, invalidated;
	assert(dbus_r_array_begin(&r, "{sv}", &changed) && dbus_r_array_more(&r, &changed));
	assert(dbus_r_string(&r, text, sizeof(text)) && !strcmp(text, "Metadata"));
	assert(dbus_r_signature(&r, signature, sizeof(signature)) && !strcmp(signature, "a{sv}"));
	assert(dbus_r_array_begin(&r, "{sv}", &metadata));
	bool saw_title = false, saw_artist = false, saw_album = false, saw_length = false;
	while (dbus_r_array_more(&r, &metadata)) {
		assert(dbus_r_string(&r, text, sizeof(text)));
		assert(dbus_r_signature(&r, signature, sizeof(signature)));
		if (!strcmp(text, "xesam:title")) {
			assert(!strcmp(signature, "s") && dbus_r_string(&r, text, sizeof(text)));
			assert(!strcmp(text, title));
			saw_title = true;
		} else if (!strcmp(text, "xesam:artist")) {
			assert(!strcmp(signature, "as") && dbus_r_array_begin(&r, "s", &artists));
			assert(dbus_r_array_more(&r, &artists) && dbus_r_string(&r, text, sizeof(text)));
			assert(!strcmp(text, artist) && !dbus_r_array_more(&r, &artists));
			saw_artist = true;
		} else if (!strcmp(text, "xesam:album")) {
			assert(!strcmp(signature, "s") && dbus_r_string(&r, text, sizeof(text)));
			assert(!strcmp(text, album));
			saw_album = true;
		} else if (!strcmp(text, "mpris:length")) {
			assert(!strcmp(signature, "x"));
			r_pad(&r, 8);
			assert(!r.bad && r.at + sizeof(int64_t) <= r.len);
			int64_t actual;
			memcpy(&actual, r.data + r.at, sizeof(actual));
			r.at += sizeof(actual);
			assert(actual == length_us);
			saw_length = true;
		} else {
			assert(!strcmp(text, "mpris:trackid") && !strcmp(signature, "o"));
			assert(dbus_r_string(&r, text, sizeof(text)) && !strcmp(text, PLAYER_PATH "/track"));
		}
	}
	assert(saw_title == (title[0] != '\0') && saw_artist == (artist[0] != '\0'));
	assert(saw_album == (album[0] != '\0') && saw_length == (length_us > 0));
	assert(!dbus_r_array_more(&r, &changed));
	assert(dbus_r_array_begin(&r, "s", &invalidated) && !dbus_r_array_more(&r, &invalidated));
	assert(!r.bad && r.at == r.len);
}

static void expect_change(const char *title, const char *artist, const char *album, int64_t length_us) {
	unsigned before = signals;
	update_metadata();
	assert(signals == before + 1);
	check_wire(title, artist, album, length_us);
	for (int i = 0; i < 100; i++) {
		fake_state.progress_current_secs += 0.5;
		update_metadata();
	}
	assert(signals == before + 1);
}

int main(void) {
	// Transport is mocked; production snapshots, change detection and wire encoding run.
	conn = (dbus_conn_t *)&wire;
	fake_state.status = AUDIO_STATUS_PLAYING;
	snprintf(fake_state.current_file, sizeof(fake_state.current_file), "/cache/144069784.m4a");
	expect_change("144069784.m4a", "", "", 0);

	// Each late field alone must update even when the cached audio path is identical.
	snprintf(fake_state.metadata.title, sizeof(fake_state.metadata.title), "Correct title");
	expect_change("Correct title", "", "", 0);
	snprintf(fake_state.metadata.album_artist, sizeof(fake_state.metadata.album_artist), "Album artist");
	expect_change("Correct title", "Album artist", "", 0);
	snprintf(fake_state.metadata.artist, sizeof(fake_state.metadata.artist), "Track artist");
	expect_change("Correct title", "Track artist", "", 0);
	snprintf(fake_state.metadata.album, sizeof(fake_state.metadata.album), "Correct album");
	expect_change("Correct title", "Track artist", "Correct album", 0);
	fake_state.progress_total_secs = 123.5;
	expect_change("Correct title", "Track artist", "Correct album", 123500000);

	// A different file with identical tags still changes identity; pause/progress do not.
	snprintf(fake_state.current_file, sizeof(fake_state.current_file), "/cache/another.m4a");
	expect_change("Correct title", "Track artist", "Correct album", 123500000);
	unsigned before = signals;
	fake_state.status = AUDIO_STATUS_PAUSED;
	update_metadata();
	assert(signals == before);

	// Live station identity stays fixed while its on-air artist/title line changes.
	fake_state.live = true;
	fake_state.current_file[0] = '\0';
	snprintf(fake_state.metadata.title, sizeof(fake_state.metadata.title), "Station");
	snprintf(fake_state.metadata.artist, sizeof(fake_state.metadata.artist), "On air one");
	fake_state.metadata.album[0] = '\0';
	expect_change("Station", "On air one", "", 0);
	snprintf(fake_state.metadata.artist, sizeof(fake_state.metadata.artist), "On air two");
	expect_change("Station", "On air two", "", 0);
	snprintf(fake_state.metadata.title, sizeof(fake_state.metadata.title), "Other station");
	expect_change("Other station", "On air two", "", 0);

	// A real teardown must invalidate the last sent snapshot before reconnect.
	registered = true;
	teardown();
	assert(!conn && !registered && !have_sent_track && unregisters == 1 && disconnects == 1);
	conn = (dbus_conn_t *)&wire;
	assert(register_with_bluez() && registrations == 1);
	expect_change("Other station", "On air two", "", 0);

	// Removing all loaded media clears metadata once, including after an empty reconnect.
	memset(&fake_state, 0, sizeof(fake_state));
	expect_change("", "", "", 0);
	registered = true;
	teardown();
	conn = (dbus_conn_t *)&wire;
	assert(register_with_bluez() && registrations == 2);
	expect_change("", "", "", 0);
	puts("PASS btplayer metadata: late same-path fields, repeats, radio and reconnect");
	return 0;
}
