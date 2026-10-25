// Real PCM parser, signal cache and owner/endpoint invalidation; no socket.
// Compile like btstack_protocol_test.c, including btrate.c and --gc-sections.
#define main original_protocol_main
#include "btstack_protocol_test.c"
#undef main

static dbus_conn_t live_connection;
static unsigned notifications;
static void notified(void) { notifications++; }
static const char *path = "/org/bluealsa/hci0/dev_AA_BB_CC_DD_EE_FF/a2dpsrc/sink";

static void bitrate_signal(dbus_conn_t *source, unsigned value, bool invalidate, bool malformed) {
	dbus_writer_t body = {0};
	dbus_array_t props, invalid;
	dbus_w_string(&body, BALSA_PCM_IFACE);
	dbus_w_array_begin(&body, "{sv}", &props);
	if (!invalidate) {
		dbus_w_dict_variant_begin(&body, "CodecBitrate", "u");
		dbus_w_u32(&body, value);
	}
	dbus_w_array_end(&body, &props);
	dbus_w_array_begin(&body, "s", &invalid);
	if (invalidate)
		dbus_w_string(&body, "CodecBitrate");
	dbus_w_array_end(&body, &invalid);
	dbus_msg_t message = {.type = DBUS_TYPE_SIGNAL, .body = body.buf,
		.body_len = body.used - (malformed ? 1 : 0)};
	snprintf(message.path, sizeof(message.path), "%s", path);
	snprintf(message.interface, sizeof(message.interface), "%s", PROPS_IFACE);
	snprintf(message.member, sizeof(message.member), "PropertiesChanged");
	on_signal(source, &message, NULL);
}

static void replace_endpoint(bool add) {
	dbus_writer_t body = {0};
	dbus_array_t interfaces;
	dbus_w_string(&body, path);
	dbus_w_array_begin(&body, add ? "{sa{sv}}" : "s", &interfaces);
	if (add) {
		dbus_array_t props;
		dbus_w_string(&body, BALSA_PCM_IFACE);
		dbus_w_array_begin(&body, "{sv}", &props);
		dbus_w_array_end(&body, &props);
	} else {
		dbus_w_string(&body, BALSA_PCM_IFACE);
	}
	dbus_w_array_end(&body, &interfaces);
	dbus_msg_t message = {.type = DBUS_TYPE_SIGNAL, .body = body.buf, .body_len = body.used};
	snprintf(message.interface, sizeof(message.interface), "%s", OBJMAN_IFACE);
	snprintf(message.member, sizeof(message.member), "%s", add ? "InterfacesAdded" : "InterfacesRemoved");
	on_signal(&live_connection, &message, NULL);
}

static void newer_sample(void) { bitrate_signal(&live_connection, 303000, false, false); }

int main(void) {
	assert(original_protocol_main() == 0);
	conn = &live_connection;
	service_changed = false;
	pcm_count = 1;
	snprintf(pcm_addr[0], BT_ADDR_MAX, "%s", address);
	pcm_bitrate_count = 0;
	change_cb = notified;
	unsigned calls = manager_calls;
	assert(btstack_codec_bitrate(address) == 0);
	bitrate_signal(conn, 909000, false, false);
	assert(btstack_codec_bitrate(address) == 909000 && notifications == 0 && calls == manager_calls);
	bitrate_signal(conn, 606000, false, false);
	assert(btstack_codec_bitrate(address) == 606000 && notifications == 0);
	const char *first_path = path;
	pcm_count = 2;
	snprintf(pcm_addr[1], BT_ADDR_MAX, "11:22:33:44:55:66");
	path = "/org/bluealsa/hci0/dev_11_22_33_44_55_66/a2dpsrc/sink";
	bitrate_signal(conn, 330000, false, false);
	assert(btstack_codec_bitrate("11:22:33:44:55:66") == 330000 && btstack_codec_bitrate(address) == 606000);
	path = first_path;
	bitrate_signal(conn, 1, false, true);
	assert(btstack_codec_bitrate(address) == 606000);
	dbus_conn_t retired = {0};
	bitrate_signal(&retired, 990000, false, false);
	assert(btstack_codec_bitrate(address) == 606000);
	bitrate_signal(conn, 0, true, false);
	assert(btstack_codec_bitrate(address) == 0);
	bitrate_signal(conn, 909000, false, false);
	replace_endpoint(false);
	assert(btstack_codec_bitrate(address) == 0);
	bitrate_signal(conn, 909000, false, false);
	assert(btstack_codec_bitrate(address) == 0);
	replace_endpoint(true);
	assert(btstack_codec_bitrate(address) == 0);
	// A property reply from before a newer signal must not overwrite its cache.
	memset(&response, 0, sizeof(response));
	dbus_array_t props;
	dbus_w_array_begin(&response, "{sv}", &props);
	dbus_w_dict_string(&response, "Codec", "LDAC");
	dbus_w_dict_variant_begin(&response, "CodecBitrate", "u");
	dbus_w_u32(&response, 909000);
	dbus_w_array_end(&response, &props);
	before_reply_read = newer_sample;
	btstack_stream_t info;
	assert(btstack_stream_info(address, false, &info));
	before_reply_read = NULL;
	assert(btstack_codec_bitrate(address) == 303000);
	// An older daemon without the optional property remains usable and unknown.
	stream_info(true);
	assert(btstack_codec_bitrate(address) == 0);
	bitrate_signal(conn, 909000, false, false);
	memset(&response, 0, sizeof(response));
	dbus_array_t objects;
	dbus_w_array_begin(&response, "{oa{sa{sv}}}", &objects);
	dbus_w_array_end(&response, &objects);
	btstack_refresh_audio();
	assert(pcm_bitrate_count == 0 && btstack_codec_bitrate(address) == 0);
	replace_endpoint(true);
	bitrate_signal(conn, 909000, false, false);
	pthread_mutex_lock(&lock);
	invalidate_caches_locked();
	pthread_mutex_unlock(&lock);
	assert(btstack_codec_bitrate(address) == 0);
	service_changed = true;
	assert(btstack_codec_bitrate(address) == 0 && btstack_codec_bitrate(NULL) == 0);
	puts("PASS measured bitrate, old peers, no telemetry wake/RPC, stale replies, endpoint and owner retirement");
	return 0;
}
