// cc -std=gnu11 -O1 -g -fsanitize=address,undefined -I. tests/config_store_concurrency_test.c -pthread -o /tmp/config_test
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static int checked_sync(int fd);
static int checked_close(FILE *f);
#define fsync checked_sync
#define fclose checked_close
#include "src/system/core/config.c"
#undef fsync
#undef fclose

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static bool block_sync, entered, released, fail_close;
static int fail_sync;
static int checked_sync(int fd) {
	if (fail_sync && --fail_sync == 0) {
		errno = ENOSPC;
		return -1;
	}
	pthread_mutex_lock(&gate);
	if (block_sync) {
		block_sync = false;
		entered = true;
		pthread_cond_broadcast(&ready);
		while (!released)
			pthread_cond_wait(&ready, &gate);
	}
	pthread_mutex_unlock(&gate);
	return fsync(fd);
}
static int checked_close(FILE *f) {
	int result = fclose(f);
	if (fail_close) {
		fail_close = false;
		errno = ENOSPC;
		return EOF;
	}
	return result;
}
static void *save_thread(void *unused) {
	(void)unused;
	assert(config_save());
	return NULL;
}
static void *set_thread(void *arg) {
	const char *key = arg;
	for (int i = 0; i < 300; i++) {
		config_set_int("parallel", key, i);
		char own[128];
		assert(config_get_copy("parallel", key, NULL, own, sizeof(own)));
	}
	return NULL;
}
int main(void) {
	char root[] = "/tmp/sonix-config-XXXXXX";
	assert(mkdtemp(root));
	char path[512], ebook[512], temp[560];
	snprintf(path, sizeof(path), "%s/device.ini", root);
	snprintf(ebook, sizeof(ebook), "%s/ebook.ini", root);
	snprintf(temp, sizeof(temp), "%s.tmp", path);
	assert(!config_init(path));
	config_set("test", "value", "old");
	assert(config_save());
	char own[128];
	assert(config_get_copy("test", "value", NULL, own, sizeof(own)));
	config_set("test", "value", "new");
	assert(!strcmp(own, "old"));
	block_sync = true;
	pthread_t saver;
	assert(!pthread_create(&saver, NULL, save_thread, NULL));
	pthread_mutex_lock(&gate);
	while (!entered)
		pthread_cond_wait(&ready, &gate);
	pthread_mutex_unlock(&gate);
	// A save must not hold the RAM lock, nor clear this later change's dirty bit.
	config_set("test", "value", "latest");
	pthread_mutex_lock(&gate);
	released = true;
	pthread_cond_broadcast(&ready);
	pthread_mutex_unlock(&gate);
	pthread_join(saver, NULL);
	assert(g_main.dirty);
	assert(config_save());
	assert(!g_main.dirty);
	for (int failure = 1; failure <= 3; failure++) {
		config_set_int("test", "failure", failure);
		if (failure == 3)
			fail_close = true;
		else
			fail_sync = failure;
		assert(!config_save());
		assert(g_main.dirty);
		assert(access(temp, F_OK) != 0);
		assert(config_save());
		assert(!g_main.dirty);
	}
	pthread_t a, b;
	assert(!pthread_create(&a, NULL, set_thread, "gui"));
	assert(!pthread_create(&b, NULL, set_thread, "worker"));
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	assert(config_save());
	assert(config_get_int("parallel", "gui", -1) == 299);
	assert(config_get_int("parallel", "worker", -1) == 299);
	assert(!config_store_init(config_ebook_store(), ebook));
	config_set("ebook", "position", "123");
	assert(config_save());
	fail_sync = 1;
	assert(config_store_move_section(config_main_store(), config_ebook_store(), "ebook") == 0);
	assert(config_get_int("ebook", "position", -1) == 123);
	assert(config_store_move_section(config_main_store(), config_ebook_store(), "ebook") == 1);
	assert(config_get_int("ebook", "position", -1) == -1);
	assert(config_save());
	assert(config_init(path));
	assert(config_get_int("parallel", "worker", -1) == 299);
	assert(config_store_get_int(config_ebook_store(), "ebook", "position", -1) == 123);
	unlink(path);
	unlink(ebook);
	rmdir(root);
	puts("PASS configuration snapshots, concurrent updates, sync/close failure and migration");
}
