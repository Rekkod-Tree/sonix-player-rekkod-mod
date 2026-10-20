// cc -O2 -std=gnu11 -I. tests/openssl_runtime_test.c
// src/system/net/{openssl_runtime,crypto}.c -ldl -pthread -o /tmp/openssl_runtime_test
#include "src/system/net/crypto.h"
#include "src/system/net/openssl_runtime.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
	void *ssl = NULL, *crypto = NULL;
	bool ok = openssl_runtime_get(&ssl, &crypto);
	if (argc == 2 && !strcmp(argv[1], "unavailable")) {
		assert(!ok && !ssl && !crypto);
		puts("PASS incomplete requested pair rejected without fallback");
		return 0;
	}
	assert(ok && ssl && crypto);
	assert(dlsym(ssl, "OpenSSL_version") == dlsym(crypto, "OpenSSL_version"));
	assert(crypto_available());
	int (*random_bytes)(unsigned char *, int);
	void *random_symbol = dlsym(crypto, "RAND_bytes");
	assert(random_symbol);
	memcpy(&random_bytes, &random_symbol, sizeof(random_bytes));
	unsigned char random_data[32] = {0}, zeros[32] = {0};
	assert(random_bytes(random_data, sizeof(random_data)) == 1 && memcmp(random_data, zeros, sizeof(zeros)));
	unsigned char key[32] = {0}, iv[16] = {0}, input[257], encrypted[257], decoded[257], window[201];
	for (int i = 0; i < 257; i++)
		input[i] = (unsigned char)i;
	for (size_t size = 16; size <= 32; size += 16) {
		assert(crypto_aes_ctr(key, size, iv, input, encrypted, sizeof(input), 0));
		assert(crypto_aes_ctr(key, size, iv, encrypted, decoded, sizeof(input), 0));
		assert(!memcmp(input, decoded, sizeof(input)));
		assert(crypto_aes_ctr(key, size, iv, input + 17, window, sizeof(window), 17));
		assert(!memcmp(encrypted + 17, window, sizeof(window)));
	}
	void *ssl2, *crypto2;
	assert(openssl_runtime_get(&ssl2, &crypto2));
	assert(ssl2 == ssl && crypto2 == crypto);
	puts("PASS shared matched pair; NIST self-tests, AES128/256 roundtrip and unaligned resume");
}
