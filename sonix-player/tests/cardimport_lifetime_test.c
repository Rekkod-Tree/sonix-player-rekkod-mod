// cc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -I. tests/cardimport_lifetime_test.c -Wl,--gc-sections -pthread -o /tmp/cardimport_lifetime
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
static int checked_close(FILE *f);
#define fclose checked_close
#include "src/system/streaming/cardimport.c"
#undef fclose
const char *tr(const char *s) { return s; }
void thread_be_low_priority(const char *s) { (void)s; }
static bool online = true, close_error;
bool streamerr_online(void) { return online; }
static int checked_close(FILE *f) {
	int r = fclose(f);
	if (close_error) {
		close_error = false;
		return EOF;
	}
	return r;
}
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static bool waiting, entered, released;
static cardimport_result_t answer;
static cardimport_result_t validate(const char lines[][CARDIMPORT_LINE_MAX], int count, char *message, size_t size) {
	(void)lines;
	(void)message;
	(void)size;
	assert(count == 1);
	pthread_mutex_lock(&gate);
	if (waiting) {
		entered = true;
		pthread_cond_broadcast(&cv);
		while (!released)
			pthread_cond_wait(&cv, &gate);
	}
	pthread_mutex_unlock(&gate);
	return answer;
}
static char card[512], path[600], bad[640];
static entry_t entry = {.service = "fixture", .file_name = "test.txt", .validate = validate};
static unsigned long epoch;
static void put(const char *text) {
	FILE *f = fopen(path, "wb");
	assert(f);
	fputs(text, f);
	assert(!fclose(f));
}
static void *run(void *unused) {
	(void)unused;
	attempt(&entry, card, true, epoch);
	return NULL;
}
int main(void) {
	char tmp[] = "/tmp/sonix-cardimport-XXXXXX";
	assert(mkdtemp(tmp));
	snprintf(card, sizeof(card), "%s", tmp);
	snprintf(path, sizeof(path), "%s/test.txt", card);
	snprintf(bad, sizeof(bad), "%s.bad", path);
	cardimport_set_root(card);
	for (int change = 0; change < 3; change++) {
		put("original-fixture\n");
		epoch = card_epoch;
		answer = CARDIMPORT_OK;
		waiting = true;
		entered = released = false;
		pthread_t t;
		assert(!pthread_create(&t, NULL, run, NULL));
		pthread_mutex_lock(&gate);
		while (!entered)
			pthread_cond_wait(&cv, &gate);
		pthread_mutex_unlock(&gate);
		if (change == 0)
			cardimport_set_root(NULL);
		if (change != 2)
			assert(!unlink(path));
		put("replaced-fixture\n"); // Includes a same-inode, same-length replacement.
		pthread_mutex_lock(&gate);
		released = true;
		pthread_cond_broadcast(&cv);
		pthread_mutex_unlock(&gate);
		pthread_join(t, NULL);
		assert(!access(path, F_OK));
		assert(access(bad, F_OK));
		cardimport_set_root(card);
	}
	waiting = false;
	for (int result = CARDIMPORT_OK; result <= CARDIMPORT_INVALID; result++) {
		put("fixture\n");
		answer = result;
		epoch = card_epoch;
		attempt(&entry, card, true, epoch);
		if (result == CARDIMPORT_OK)
			assert(access(path, F_OK));
		else if (result == CARDIMPORT_REFUSED || result == CARDIMPORT_INVALID) {
			assert(access(path, F_OK));
			assert(!access(bad, F_OK));
			unlink(bad);
		} else
			assert(!access(path, F_OK));
	}
	put("fixture\n");
	close_error = true;
	epoch = card_epoch;
	attempt(&entry, card, true, epoch);
	assert(!access(path, F_OK));
	assert(access(bad, F_OK));
	unlink(path);
	cardimport_set_root(NULL);
	rmdir(card);
	puts("PASS importer detach, replacement, in-place edits, outcomes and read-close failure");
}
