// Actual training module test; no real GCOV writes or fixed /tmp marker.
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *test_marker_open(const char *, const char *);
static int test_marker_close(FILE *);
#define fopen test_marker_open
#define fclose test_marker_close
#include SONIX_PGO_TRAIN_SOURCE
#undef fclose
#undef fopen

static pthread_mutex_t runtime_flush_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int flush_called, flush_returned, marker_opened, marker_closed, dump_called;

void __gcov_flush(void) {
	atomic_fetch_add(&flush_called, 1);
	assert(pthread_mutex_lock(&runtime_flush_lock) == 0);
	atomic_store(&flush_returned, 1);
	assert(pthread_mutex_unlock(&runtime_flush_lock) == 0);
}
void __gcov_dump(void) {
	atomic_fetch_add(&dump_called, 1);
	assert(!"unserialized dump entry was called");
}
static FILE *test_marker_open(const char *name, const char *mode) {
	assert(strcmp(name, "/tmp/sonix-pgo-complete") == 0);
	assert(strcmp(mode, "we") == 0);
	assert(atomic_load(&flush_returned) == 1);
	atomic_fetch_add(&marker_opened, 1);
	FILE *file = tmpfile();
	assert(file);
	return file;
}
static int test_marker_close(FILE *file) {
	assert(fflush(file) == 0);
	rewind(file);
	long recorded = -1;
	assert(fscanf(file, "%ld", &recorded) == 1);
	assert(recorded == (long)getpid());
	int result = fclose(file);
	atomic_fetch_add(&marker_closed, 1);
	return result;
}
static void wait_for(atomic_int *value) {
	for (int i = 0; i < 2000 && !atomic_load(value); i++) {
		struct timespec delay = {0, 1000000};
		nanosleep(&delay, NULL);
	}
	assert(atomic_load(value) == 1);
}
int main(void) {
	sigset_t current;
	assert(pthread_sigmask(SIG_BLOCK, NULL, &current) == 0);
	assert(sigismember(&current, SIGUSR1) == 1);
	assert(pthread_mutex_lock(&runtime_flush_lock) == 0);
	assert(kill(getpid(), SIGUSR1) == 0);
	wait_for(&flush_called);
	assert(!atomic_load(&flush_returned));
	assert(!atomic_load(&marker_opened));
	assert(pthread_mutex_unlock(&runtime_flush_lock) == 0);
	wait_for(&marker_closed);
	assert(kill(getpid(), SIGUSR1) == 0);
	struct timespec delay = {0, 100000000};
	nanosleep(&delay, NULL);
	assert(atomic_load(&flush_called) == 1);
	assert(atomic_load(&marker_opened) == 1);
	assert(atomic_load(&marker_closed) == 1);
	assert(!atomic_load(&dump_called));
	puts("PASS actual constructor/SIGUSR1/serialized-entry/marker-after-return/one-shot");
	return 0;
}
