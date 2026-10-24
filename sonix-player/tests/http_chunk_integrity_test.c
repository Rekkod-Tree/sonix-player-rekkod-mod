// cc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -I. tests/http_chunk_integrity_test.c -Wl,--gc-sections -pthread -lresolv -o /tmp/http_chunks
#define _GNU_SOURCE 1
#include "src/system/net/http.c"
#include <assert.h>
const char *tr(const char *s) { return s; }
tls_conn_t *tls_client(int fd, const char *host, char *err, size_t n) {
	(void)fd;
	(void)host;
	(void)err;
	(void)n;
	return NULL;
}
int tls_read(tls_conn_t *c, void *p, int n) {
	(void)c;
	(void)p;
	(void)n;
	return -1;
}
int tls_write(tls_conn_t *c, const void *p, int n) {
	(void)c;
	(void)p;
	(void)n;
	return -1;
}
void tls_free(tls_conn_t *c) { (void)c; }
static const char *reply;
static int listener;
static bool close_framed, reset_peer, stall_peer;
static void *serve(void *unused) {
	(void)unused;
	int fd = accept(listener, NULL, NULL);
	assert(fd >= 0);
	char request[4096];
	assert(recv(fd, request, sizeof(request), 0) > 0);
	const char *headers = close_framed ? "HTTP/1.0 200 OK\r\n\r\n" : "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
	assert(send(fd, headers, strlen(headers), MSG_NOSIGNAL) == (ssize_t)strlen(headers));
	send(fd, reply, strlen(reply), MSG_NOSIGNAL);
	if (stall_peer)
		usleep(2500000);
	if (reset_peer) {
		// Let the client consume the prefix before simulating a broken peer.
		usleep(20000);
		struct linger reset = {.l_onoff = 1, .l_linger = 0};
		assert(!setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)));
	}
	close(fd);
	return NULL;
}
static void check(const char *chunks, size_t limit, bool expected, const char *text) {
	reply = chunks;
	listener = socket(AF_INET, SOCK_STREAM, 0);
	assert(listener >= 0);
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	assert(!bind(listener, (void *)&addr, sizeof(addr)));
	assert(!listen(listener, 1));
	socklen_t n = sizeof(addr);
	assert(!getsockname(listener, (void *)&addr, &n));
	pthread_t server;
	assert(!pthread_create(&server, NULL, serve, NULL));
	char url[128];
	snprintf(url, sizeof(url), "http://127.0.0.1:%u/test", ntohs(addr.sin_port));
	char *body = NULL;
	size_t size = 0;
	bool actual = http_get(url, &body, &size, limit, 2);
	if (actual != expected)
		fprintf(stderr, "close=%d reset=%d limit=%zu expected=%d actual=%d\n", close_framed, reset_peer, limit, expected, actual);
	assert(actual == expected);
	if (expected)
		assert(body && size == strlen(text) && !strcmp(body, text));
	else
		assert(!body && size == 0);
	free(body);
	pthread_join(server, NULL);
	close(listener);
}
int main(void) {
	check("4\r\nGOOD\r\n0\r\n\r\n", 4096, true, "GOOD");
	check("2;x=y\r\nGO\r\n2\r\nOD\r\n0\r\nX-Test: yes\r\n\r\n", 4096, true, "GOOD");
	const char *bad[] = {"4\r\nGOOD\r\n8\r\nBAD", "4\r\nGOOD\r\n", "4\r\nGOODxx0\r\n\r\n", "-4\r\n", "+4\r\nGOOD\r\n0\r\n\r\n", "xyz\r\n", "4junk\r\nGOOD\r\n0\r\n\r\n", "0\r\n", "0\r\nX: y\r\n", "4\nGOOD\r\n0\r\n\r\n", "ffffffffffffffffffffffffffffffff\r\n"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		check(bad[i], 4096, false, NULL);
	check("10\r\nGOODprefix", 4, true, "GOOD");
	http_set_thread_body_cap(4);
	check("10\r\nGOODprefix", 4096, true, "GOOD");
	http_set_thread_body_cap(0);
	check("8\r\nGOODtext\r\n0\r\n\r\n", 4096, true, "GOODtext");
	close_framed = true;
	check("GOODtext", 4096, true, "GOODtext");
	reset_peer = true;
	check("GOODtext", 4, true, "GOOD");
	check("partial-response", 4096, false, NULL);
	assert(outcome.fail == HTTP_FAIL_BROKEN);
	reset_peer = false;
	stall_peer = true;
	check("partial-response", 4096, false, NULL);
	assert(outcome.fail == HTTP_FAIL_TIMEOUT);
	stall_peer = false;
	check("GOODtext", 4096, true, "GOODtext");
	puts("PASS HTTP chunk/close framing, clean EOF, peer reset, timeout and bounded prefix");
}
