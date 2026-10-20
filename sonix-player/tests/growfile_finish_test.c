// Build: cc -std=gnu11 -Wall -Wextra -I. tests/growfile_finish_test.c src/system/decode/growfile.c -pthread -o /tmp/growfile_finish_test
// Run: /tmp/growfile_finish_test
#define _GNU_SOURCE 1
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "src/system/decode/growfile.h"

static char path[256];
static void *finish(void *unused) {
	(void)unused;
	usleep(50000);
	FILE *file = fopen(path, "ab");
	assert(file && fwrite("bc", 1, 2, file) == 2 && fclose(file) == 0);
	growfile_progress(path, 3);
	growfile_finish(path, true);
	return NULL;
}

int main(void) {
	snprintf(path, sizeof(path), "/tmp/sonix-grow-finish-%ld", (long)getpid());
	FILE *file = fopen(path, "wx");
	assert(file && fwrite("a", 1, 1, file) == 1 && fclose(file) == 0);
	growfile_announce(path, 3);
	growfile_progress(path, 1);
	growfile_t *reader = growfile_open(path);
	assert(reader);
	pthread_t writer;
	assert(pthread_create(&writer, NULL, finish, NULL) == 0);
	char bytes[4] = {0};
	assert(growfile_read(reader, bytes, 3) == 3);
	assert(strcmp(bytes, "abc") == 0);
	assert(growfile_available(reader) == 3);
	assert(growfile_read(reader, bytes, 1) == 0);
	growfile_close(reader);
	pthread_join(writer, NULL);
	unlink(path);
	puts("growfile final-chunk race PASS");
	return 0;
}
