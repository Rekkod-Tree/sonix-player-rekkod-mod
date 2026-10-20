// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I.
// tests/btstack_protocol_test.c src/system/bluetooth/btrate.c -pthread
// -Wl,--gc-sections -o /tmp/btstack_protocol_test
#include "src/system/bluetooth/bapcm.c"
#include "src/system/bluetooth/dbuslite.c"
#include <assert.h>
static dbus_writer_t request, response;
static dbus_writer_t *fake_begin(dbus_conn_t *c, const char *dest, const char *path, const char *interface, const char *member, const char *signature) {
	(void)c;
	(void)dest;
	(void)path;
	(void)interface;
	(void)member;
	(void)signature;
	memset(&request, 0, sizeof(request));
	return &request;
}
static bool fake_send(dbus_conn_t *c, int timeout, char *error, size_t length) {
	(void)c;
	(void)timeout;
	if (error && length)
		error[0] = '\0';
	return !request.overflow;
}
static void fake_reader(dbus_conn_t *c, dbus_reader_t *r) {
	(void)c;
	*r = (dbus_reader_t){.data = response.buf, .len = response.used};
}
#define dbus_call_begin fake_begin
#define dbus_call_send fake_send
#define dbus_reply_reader fake_reader
#include "src/system/bluetooth/btstack.c"
#undef dbus_call_begin
#undef dbus_call_send
#undef dbus_reply_reader
static const char *address = "AA:BB:CC:DD:EE:FF";
static void stream_info(bool modern) {
	memset(&response, 0, sizeof(response));
	dbus_array_t props;
	dbus_w_array_begin(&response, "{sv}", &props);
	dbus_w_dict_string(&response, "Codec", "LDAC");
	dbus_w_dict_variant_begin(&response, modern ? "Rate" : "Sampling", "u");
	dbus_w_u32(&response, 96000);
	dbus_w_dict_variant_begin(&response, "Channels", "y");
	dbus_w_byte(&response, 2);
	dbus_w_dict_variant_begin(&response, "Format", "q");
	dbus_w_u16(&response, 0x8420);
	dbus_w_dict_variant_begin(&response, "Volume", modern ? "ay" : "q");
	if (modern) {
		dbus_array_t bytes;
		dbus_w_array_begin(&response, "y", &bytes);
		dbus_w_byte(&response, 0xb3);
		dbus_w_byte(&response, 0x2f);
		dbus_w_array_end(&response, &bytes);
	} else
		dbus_w_u16(&response, 0xb32f);
	dbus_w_array_end(&response, &props);
	btstack_stream_t result;
	assert(btstack_stream_info(address, false, &result));
	assert(result.rate == 96000 && result.channels == 2 && result.bits == 32 && !strcmp(result.codec, "LDAC"));
	char info[128];
	assert(btstack_audio_info(address, info, sizeof(info)) && strstr(info, "96000 Hz"));
}
static void codec_rates(bool modern) {
	memset(&response, 0, sizeof(response));
	dbus_array_t codecs, props, rates;
	dbus_w_array_begin(&response, "{sa{sv}}", &codecs);
	dbus_w_string(&response, "LDAC");
	dbus_w_array_begin(&response, "{sv}", &props);
	dbus_w_dict_variant_begin(&response, modern ? "Rates" : "SupportedSampling", "au");
	dbus_w_array_begin(&response, "u", &rates);
	dbus_w_u32(&response, 44100);
	dbus_w_u32(&response, 48000);
	dbus_w_u32(&response, 96000);
	dbus_w_array_end(&response, &rates);
	dbus_w_array_end(&response, &props);
	dbus_w_array_end(&response, &codecs);
	unsigned values[4] = {0};
	assert(btstack_codec_rates(address, "LDAC", values, 4) == 3);
	assert(values[0] == 44100 && values[1] == 48000 && values[2] == 96000);
}
static void restore_volume(bool modern) {
	memset(&response, 0, sizeof(response));
	bapcm_volume_t original = {.values = {0xb3, 0x2f}, .count = 2, .array = modern};
	bapcm_volume_write(&response, &original);
	bapcm_volume_t saved;
	assert(btstack_pcm_volume_get(address, true, &saved));
	assert(saved.count == 2 && !memcmp(saved.values, original.values, 2));
	saved.array = !modern; // The new endpoint's protocol wins over cached format.
	assert(btstack_pcm_volume_set(address, true, &saved));
	dbus_reader_t reader = {.data = request.buf, .len = request.used};
	char text[80], signature[16];
	assert(dbus_r_string(&reader, text, sizeof(text)) && !strcmp(text, BALSA_PCM_IFACE));
	assert(dbus_r_string(&reader, text, sizeof(text)) && !strcmp(text, "Volume"));
	assert(dbus_r_signature(&reader, signature, sizeof(signature)));
	bapcm_volume_t restored;
	assert(bapcm_volume_read(&reader, signature, &restored));
	assert(restored.array == modern && restored.count == 2 && !memcmp(restored.values, original.values, 2));
}
static void select_rate(void) {
	assert(btstack_select_codec_rate(address, "LDAC", 96000));
	dbus_reader_t reader = {.data = request.buf, .len = request.used};
	char text[80], signature[16];
	assert(dbus_r_string(&reader, text, sizeof(text)) && !strcmp(text, "LDAC"));
	dbus_array_iter_t props;
	assert(dbus_r_array_begin(&reader, "{sv}", &props));
	int rates = 0;
	while (dbus_r_array_more(&reader, &props)) {
		assert(dbus_r_string(&reader, text, sizeof(text)) && dbus_r_signature(&reader, signature, sizeof(signature)));
		if (!strcmp(text, "Rate") || !strcmp(text, "Sampling")) {
			uint32_t rate;
			assert(!strcmp(signature, "u") && dbus_r_u32(&reader, &rate) && rate == 96000);
			rates++;
		} else
			assert(dbus_r_skip(&reader, signature));
	}
	assert(rates == 2 && !reader.bad);
}
int main(void) {
	conn = (dbus_conn_t *)(uintptr_t)1;
	for (int modern = 0; modern <= 1; modern++) {
		stream_info(modern);
		codec_rates(modern);
		restore_volume(modern);
	}
	select_rate();
	puts("PASS BlueALSA4/5 actual Sonix PCM info, codec rates, rate selection and raw volume restore");
}
