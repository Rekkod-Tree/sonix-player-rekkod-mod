/* Native R1 test; include src/ from the patched BlueALSA build.
 * Link the real decoder BEFORE the encoder, as in the shipped daemon:
 * cc -O2 -flto=2 -Wall -Wextra -Werror -I<bluealsa>/src <pkg-cflags>
 *   tests/ldac_handle_api_test.c -Wl,--no-as-needed -lldacdec
 *   -lldacBT_enc -lldacBT_abr -ldl -pthread -o /tmp/ldac_handle_api_test
 * Run with both runtime libraries present. Never compile with NDEBUG.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ldacBT_abr.h>
#include "ldac-handle-api.h"

#ifdef NDEBUG
#error This regression test requires active assertions.
#endif

/* The shipped decoder exposes this compatibility signature. */
int ldacBT_init_handle_decode(HANDLE_LDAC_BT, int, int, int, int, int);
int ldacBT_decode(HANDLE_LDAC_BT, unsigned char *, unsigned char *, LDACBT_SMPL_FMT_T, int, int *, int *);

static struct ldac_handle_api encoder, decoder;

static void *cycle(void *unused) {
	(void)unused;
	HANDLE_LDAC_BT enc = encoder.get();
	HANDLE_LDAC_BT dec = decoder.get();
	HANDLE_LDAC_ABR abr = ldac_ABR_get_handle();
	assert(enc && dec && abr);
	assert(!ldacBT_init_handle_encode(enc, 679, LDACBT_EQMID_HQ,
		LDACBT_CHANNEL_MODE_STEREO, LDACBT_SMPL_FMT_S32, 44100));
	assert(!ldacBT_init_handle_decode(dec, LDACBT_CHANNEL_MODE_STEREO, 44100, 0, 0, 0));
	assert(!ldac_ABR_Init(abr, 2));
	int32_t input[LDACBT_ENC_LSU * 2] = {0};
	unsigned char packet[4096], output[8192];
	int decoded_frames = 0;
	for (int n = 0; n < 12; n++) {
		if (n == 3) assert(!ldacBT_set_eqmid(enc, LDACBT_EQMID_MQ));
		if (n == 8) assert(!ldacBT_set_eqmid(enc, LDACBT_EQMID_HQ));
		int used = 0, bytes = 0, frames = 0;
		assert(!ldacBT_encode(enc, input, &used, packet, &bytes, &frames));
		assert(ldacBT_get_bitrate(enc) > 0);
		assert(ldac_ABR_Proc(enc, abr, 0, 1) >= 0);
		unsigned char *next = packet;
		while (frames-- > 0) {
			int consumed = 0, decoded = 0;
			assert(!ldacBT_decode(dec, next, output, LDACBT_SMPL_FMT_S32, bytes, &consumed, &decoded));
			assert(consumed > 0 && consumed <= bytes && decoded > 0 && decoded <= (int)sizeof(output));
			next += consumed;
			bytes -= consumed;
			decoded_frames++;
		}
	}
	assert(decoded_frames > 0);
	ldac_ABR_free_handle(abr);
	encoder.free(enc);
	decoder.free(dec);
	return NULL;
}

int main(void) {
	alarm(60);
	struct ldac_handle_api missing = {0};
	assert(ldac_handle_api_open(&missing, "sonix-nonexistent-codec.so") == -1);
	assert(!missing.library && !missing.get && !missing.free);
	assert(ldac_handle_api_open(&missing, "libm.so.6") == -1);
	assert(!missing.library && !missing.get && !missing.free);
	assert(!ldac_handle_api_open(&encoder, "libldacBT_enc.so.2"));
	assert(!ldac_handle_api_open(&decoder, "libldacdec.so.1"));
	assert(!ldac_handle_api_open(&encoder, "libldacBT_enc.so.2"));
	Dl_info enc_info, dec_info;
	assert(dladdr(encoder.free, &enc_info) && dladdr(decoder.free, &dec_info));
	assert(strstr(enc_info.dli_fname, "libldacBT_enc.so"));
	assert(strstr(dec_info.dli_fname, "libldacdec.so"));
	assert(encoder.get != decoder.get && encoder.free != decoder.free);
	for (int threaded = 0; threaded < 2; threaded++) {
		int baseline = 0;
		for (int n = 0; n < 128; n++) {
			if (threaded) {
				pthread_t thread;
				assert(!pthread_create(&thread, NULL, cycle, NULL));
				assert(!pthread_join(thread, NULL));
			} else cycle(NULL);
			int allocated = mallinfo().uordblks;
			if (n == 0) baseline = allocated;
			assert(allocated <= baseline + 512);
		}
	}
	puts("PASS: distinct LDAC handles, quality changes, encode/decode and bounded heap");
	return 0;
}
