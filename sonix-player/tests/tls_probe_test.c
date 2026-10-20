// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I.
// tests/tls_probe_test.c src/system/net/{tls,openssl_runtime}.c
// -ldl -pthread -Wl,--gc-sections -o /tmp/tls_probe_test
#define _GNU_SOURCE
#include "src/system/net/tls.h"
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
const char *config_get(const char *section, const char *key, const char *fallback) {
	(void)section;
	(void)key;
	const char *path = getenv("SONIX_TEST_CA_BUNDLE");
	return path ? path : fallback;
}
const char *tr(const char *key) { return key; }
int main(int argc, char **argv) {
	assert(argc == 5);
	struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *addresses = NULL;
	assert(!getaddrinfo(argv[1], argv[2], &hints, &addresses));
	int fd = -1;
	for (struct addrinfo *a = addresses; a; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0)
			continue;
		struct timeval timeout = {.tv_sec = 12};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		if (!connect(fd, a->ai_addr, a->ai_addrlen))
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(addresses);
	assert(fd >= 0);
	struct timespec before, after;
	clock_gettime(CLOCK_MONOTONIC, &before);
	char error[256] = "";
	tls_conn_t *connection = tls_client(fd, argv[3], error, sizeof(error));
	clock_gettime(CLOCK_MONOTONIC, &after);
	bool expected = !strcmp(argv[4], "success");
	assert((connection != NULL) == expected);
	struct rusage usage;
	assert(!getrusage(RUSAGE_SELF, &usage));
	printf("PASS tls expected=%s version=%s ca=%s error=%s elapsed_ms=%.3f maxrss_kib=%ld\n", argv[4], tls_version(), tls_ca_source(), error, (after.tv_sec - before.tv_sec) * 1000.0 + (after.tv_nsec - before.tv_nsec) / 1e6, usage.ru_maxrss);
	if (connection)
		tls_free(connection);
	close(fd);
}
