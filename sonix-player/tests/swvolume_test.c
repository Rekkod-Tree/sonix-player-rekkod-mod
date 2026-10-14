// cc -std=gnu11 -O2 -Wall -Wextra -I. tests/swvolume_test.c src/system/audio/swvolume.c -lm -pthread -o /tmp/swvolume_test
// Also run with -fsanitize=thread, and with -fsanitize=address,undefined separately.
#include "src/system/audio/swvolume.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static bool bluetooth, usb = true, hardware_volume;
bool audio_output_is_bluetooth(void) { return bluetooth; }
bool usbaudio_active(void) { return usb; }
bool usbaudio_has_volume_control(void) { return hardware_volume; }

static pthread_barrier_t start;
static void *writer(void *unused) {
	(void)unused;
	pthread_barrier_wait(&start);
	for (int i = 0; i < 100000; i++) {
		swvolume_set_index(i % 101);
		switch (i % 3) {
		case 0: swvolume_set_usb_gain(-600, false); break;
		case 1: swvolume_set_usb_gain(0, true); break;
		case 2: swvolume_clear_usb_gain(); break;
		}
	}
	return NULL;
}

static void *reader(void *unused) {
	(void)unused;
	pthread_barrier_wait(&start);
	for (int i = 0; i < 100000; i++) {
		int32_t block[32];
		for (int j = 0; j < 32; j++) block[j] = 1000000000;
		swvolume_apply_s32(block, 32);
		assert(block[0] >= 0 && block[0] <= 1000000000);
		for (int j = 1; j < 32; j++) assert(block[j] == block[0]);
	}
	return NULL;
}

static void packed24(void) {
	static const int32_t input[] = {-8388608, -123456, -1, 0, 1, 123456, 8388607};
	static const int32_t expected[] = {-838861, -12346, -1, 0, 0, 12345, 838860};
	unsigned char bytes[23];
	memset(bytes, 0xa5, sizeof(bytes));
	for (int i = 0; i < 7; i++) {
		uint32_t word = (uint32_t)input[i];
		for (int j = 0; j < 3; j++) bytes[1 + i * 3 + j] = word >> (j * 8);
	}
	swvolume_apply_s24_3le(bytes + 1, 7);
	assert(bytes[0] == 0xa5 && bytes[22] == 0xa5);
	for (int i = 0; i < 7; i++) {
		uint32_t word = (uint32_t)expected[i];
		for (int j = 0; j < 3; j++) assert(bytes[1 + i * 3 + j] == (unsigned char)(word >> (j * 8)));
	}
}

int main(void) {
	// USB mute must work even before the index table has been initialized.
	int32_t early[] = {INT32_MIN, -1, 0, 1, INT32_MAX};
	swvolume_set_usb_gain(0, true);
	swvolume_apply_s32(early, 5);
	for (int i = 0; i < 5; i++) assert(early[i] == 0);
	swvolume_clear_usb_gain();

	pthread_barrier_init(&start, NULL, 3);
	pthread_t threads[3];
	assert(pthread_create(&threads[0], NULL, writer, NULL) == 0);
	assert(pthread_create(&threads[1], NULL, reader, NULL) == 0);
	assert(pthread_create(&threads[2], NULL, reader, NULL) == 0);
	for (int i = 0; i < 3; i++) pthread_join(threads[i], NULL);
	pthread_barrier_destroy(&start);

	swvolume_set_usb_gain(-2000, false); // -20 dB; fixed golden PCM vectors.
	short s16[] = {-32768, -12345, -1, 0, 1, 12345, 32767};
	const short expected16[] = {-3277, -1235, -1, 0, 0, 1234, 3276};
	swvolume_apply_s16(s16, 7);
	assert(memcmp(s16, expected16, sizeof(s16)) == 0);
	unsigned char u8[] = {0, 1, 127, 128, 129, 254, 255};
	const unsigned char expected8[] = {115, 115, 127, 128, 128, 140, 140};
	swvolume_apply_u8(u8, 7);
	assert(memcmp(u8, expected8, sizeof(u8)) == 0);
	packed24();

	const int32_t original[] = {INT32_MIN, -1000000001, -1, 0, 1, 1000000001, INT32_MAX};
	int32_t copy[7];
	for (int route = 0; route < 4; route++) {
		bluetooth = route == 0;
		usb = route != 1;
		hardware_volume = route == 2;
		swvolume_clear_usb_gain();
		swvolume_set_index(route == 3 ? 100 : 0);
		memcpy(copy, original, sizeof(copy));
		swvolume_apply_s32(copy, 7);
		assert(memcmp(copy, original, sizeof(copy)) == 0);
	}
	hardware_volume = false;
	swvolume_set_index(-10);
	swvolume_apply_s32(copy, 7);
	for (int i = 0; i < 7; i++) assert(copy[i] == 0);
	swvolume_set_index(200);
	assert(swvolume_coefficient(100) == 2147483648LL);
	swvolume_set_usb_gain(1200, false); // Positive USB gains cannot amplify.
	memcpy(copy, original, sizeof(copy));
	swvolume_apply_s32(copy, 7);
	assert(memcmp(copy, original, sizeof(copy)) == 0);
	puts("PASS: early mute, concurrent block snapshots, PCM formats, bounds and bypass");
	return 0;
}
