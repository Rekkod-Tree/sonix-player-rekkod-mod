// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I. -I./lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/btvolume_protocol_test.c -pthread
// -Wl,--gc-sections -o /tmp/btvolume_protocol_test
#include "src/system/bluetooth/bapcm.c"
#include "src/system/bluetooth/btvolume.c"
#include "src/system/bluetooth/dbuslite.c"
#include <assert.h>
static void check(dbus_writer_t *writer, int expected, unsigned count, bool array) {
	dbus_reader_t reader = {.data = writer->buf, .len = writer->used};
	char signature[16];
	assert(dbus_r_signature(&reader, signature, sizeof(signature)));
	int volume = -1;
	unsigned channels = 0;
	bool actual_array = false;
	assert(decode_volume(&reader, signature, &volume, &channels, &actual_array));
	assert(volume == expected && channels == count && actual_array == array && reader.at == reader.len);
}
int main(void) {
	dbus_writer_t writer = {0};
	dbus_w_variant_u16(&writer, 0x332f);
	check(&writer, 51, 2, false);
	for (unsigned count = 1; count <= 8; count++) {
		memset(&writer, 0, sizeof(writer));
		uint8_t values[8];
		memset(values, 44, sizeof(values));
		dbus_w_variant_bytes(&writer, values, count);
		check(&writer, 44, count, true);
	}
	memset(&writer, 0, sizeof(writer));
	uint8_t muted[] = {0xb3, 0x2f};
	dbus_w_variant_bytes(&writer, muted, 2);
	check(&writer, 51, 2, true);
	memset(&writer, 0, sizeof(writer));
	dbus_w_variant_bytes(&writer, NULL, 0);
	dbus_reader_t reader = {.data = writer.buf, .len = writer.used};
	char signature[16];
	int volume;
	unsigned count;
	bool array;
	assert(dbus_r_signature(&reader, signature, sizeof(signature)) && !decode_volume(&reader, signature, &volume, &count, &array));
	for (unsigned n = 1; n <= 8; n++) {
		bapcm_volume_t original = {.count = n, .array = true}, decoded;
		for (unsigned c = 0; c < n; c++)
			original.values[c] = (uint8_t)(0x80 | (c * 9));
		memset(&writer, 0, sizeof(writer));
		bapcm_volume_write(&writer, &original);
		reader = (dbus_reader_t){.data = writer.buf, .len = writer.used};
		assert(dbus_r_signature(&reader, signature, sizeof(signature)) && bapcm_volume_read(&reader, signature, &decoded));
		assert(decoded.array && decoded.count == n && !memcmp(decoded.values, original.values, n));
	}
	memset(&writer, 0, sizeof(writer));
	uint8_t oversized[9] = {0};
	dbus_w_variant_bytes(&writer, oversized, sizeof(oversized));
	reader = (dbus_reader_t){.data = writer.buf, .len = writer.used};
	bapcm_volume_t decoded;
	assert(dbus_r_signature(&reader, signature, sizeof(signature)) && !bapcm_volume_read(&reader, signature, &decoded));
	memset(&writer, 0, sizeof(writer));
	bapcm_volume_t invalid = {.count = 1, .array = false};
	bapcm_volume_write(&writer, &invalid);
	assert(writer.overflow);
	puts("PASS BlueALSA4 packed and BlueALSA5 channel-array wire format, mono/stereo/surround, mute and empty refusal");
}
