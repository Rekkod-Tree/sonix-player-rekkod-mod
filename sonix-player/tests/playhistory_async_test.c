// Actual history component; a gated fake store makes in-flight races repeatable.
// cc -std=gnu11 -O1 -g -I. tests/playhistory_async_test.c -pthread -o /tmp/history_async
// Run each case in a fresh process: metadata, epoch, bounded, refusal, failure.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
static bool refuse_start;
static int create_writer(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) { return refuse_start ? EAGAIN : pthread_create(t, a, fn, arg); }
#define pthread_create create_writer
#include "src/system/playback/playhistory.c"
#undef pthread_create

static atomic_uint store_epoch = 10;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool blocked, entered;
static int inserts, updates, failed, stale;
static pthread_t gui_thread;
static struct {
	char path[512], title[256], artist[256];
	long long when;
} rows[128];
static char playing_path[512];
unsigned library_database_epoch(void) { return atomic_load(&store_epoch); }
bool library_track_title(const char *path, char *out, size_t size) {
	(void)path;
	(void)out;
	(void)size;
	return false;
}
bool clock_is_set(void) { return true; }
bool audio_get_listening_progress(double *p, double *length, unsigned *serial) {
	(void)p;
	(void)length;
	if (serial)
		*serial = 1;
	return true;
}
void audio_get_current_file(char *out, size_t size) { snprintf(out, size, "%s", playing_path); }
void thread_be_low_priority(const char *name) { assert(name && !pthread_equal(pthread_self(), gui_thread)); }
static void wait_gate(void) {
	assert(!pthread_equal(pthread_self(), gui_thread)); // Writes cannot be GUI fallbacks.
	pthread_mutex_lock(&gate_lock);
	entered = true;
	pthread_cond_broadcast(&gate_cond);
	while (blocked)
		pthread_cond_wait(&gate_cond, &gate_lock);
	pthread_mutex_unlock(&gate_lock);
}
long long library_history_record_at(unsigned wanted, const char *path, const char *title, const char *artist, long long when) {
	wait_gate();
	if (wanted != atomic_load(&store_epoch)) {
		stale++;
		return 0;
	}
	if (failed)
		return 0;
	assert(inserts < 128);
	snprintf(rows[inserts].path, sizeof(rows[0].path), "%s", path);
	snprintf(rows[inserts].title, sizeof(rows[0].title), "%s", title);
	snprintf(rows[inserts].artist, sizeof(rows[0].artist), "%s", artist);
	rows[inserts].when = when;
	return ++inserts;
}
int library_history_update_at(unsigned wanted, long long id, const char *path, const char *title, const char *artist) {
	wait_gate();
	assert(wanted == atomic_load(&store_epoch) && id > 0 && id <= inserts);
	if (failed)
		return 0;
	assert(!strcmp(path, rows[id - 1].path));
	snprintf(rows[id - 1].title, sizeof(rows[0].title), "%s", title);
	snprintf(rows[id - 1].artist, sizeof(rows[0].artist), "%s", artist);
	updates++;
	return 1;
}
int library_history_items(library_history_item_t **out) {
	*out = calloc((size_t)inserts, sizeof(**out));
	for (int i = 0; i < inserts; i++)
		(*out)[i] = (library_history_item_t){.id = i + 1, .played_at = rows[i].when};
	return inserts;
}
bool library_history_entry_at(unsigned wanted, long long id, char *path, size_t path_size, char *title, size_t title_size, char *artist, size_t artist_size) {
	if (wanted != atomic_load(&store_epoch) || id < 1 || id > inserts)
		return false;
	if (path && path_size)
		snprintf(path, path_size, "%s", rows[id - 1].path);
	if (title && title_size)
		snprintf(title, title_size, "%s", rows[id - 1].title);
	if (artist && artist_size)
		snprintf(artist, artist_size, "%s", rows[id - 1].artist);
	return true;
}
static void begin_blocked(void) {
	pthread_mutex_lock(&gate_lock);
	blocked = true;
	entered = false;
	pthread_mutex_unlock(&gate_lock);
}
static void await_writer(void) {
	pthread_mutex_lock(&gate_lock);
	struct timespec until;
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += 2;
	while (!entered)
		assert(pthread_cond_timedwait(&gate_cond, &gate_lock, &until) == 0);
	pthread_mutex_unlock(&gate_lock);
}
static void release_writer(void) {
	pthread_mutex_lock(&gate_lock);
	blocked = false;
	pthread_cond_broadcast(&gate_cond);
	pthread_mutex_unlock(&gate_lock);
}
static void listen(const char *path) {
	snprintf(playing_path, sizeof(playing_path), "%s", path);
	playhistory_started(path, "Initial title", "Artist", false, false);
	playhistory_heard(path);
}
int main(int argc, char **argv) {
	assert(argc == 2);
	gui_thread = pthread_self();
	if (!strcmp(argv[1], "metadata")) {
		begin_blocked();
		listen("/card/a.flac");
		await_writer();
		long long temporary = playhistory_current_id();
		assert(temporary < 0);
		// All of these must return while the filesystem deliberately waits.
		playhistory_metadata(playing_path, "Late title", "Late artist");
		playhistory_poll();
		assert(playhistory_revision() > 0);
		char title[256];
		assert(playhistory_entry(temporary, NULL, 0, title, sizeof(title), NULL, 0));
		assert(!strcmp(title, "Late title"));
		playhistory_left();
		release_writer();
		playhistory_flush();
		assert(inserts == 1 && updates == 1 && !strcmp(rows[0].title, "Late title"));
		assert(playhistory_entry(temporary, NULL, 0, title, sizeof(title), NULL, 0));
		assert(!strcmp(title, "Late title"));
	} else if (!strcmp(argv[1], "epoch")) {
		begin_blocked();
		listen("/old-card/a.flac");
		await_writer();
		long long old = playhistory_current_id();
		atomic_store(&store_epoch, 11);
		playhistory_poll();
		assert(playhistory_current_id() == 0);
		release_writer();
		playhistory_flush();
		assert(stale == 1 && inserts == 0);
		assert(!playhistory_entry(old, NULL, 0, NULL, 0, NULL, 0));
		listen("/new-card/a.flac");
		playhistory_flush();
		assert(inserts == 1 && !strcmp(rows[0].path, "/new-card/a.flac"));
	} else if (!strcmp(argv[1], "bounded")) {
		begin_blocked();
		listen("/card/first.flac");
		await_writer();
		for (int i = 0; i < 70; i++) {
			playhistory_left();
			char path[80];
			snprintf(path, sizeof(path), "/card/%d.flac", i);
			listen(path);
		}
		assert(playhistory_unsaved());
		pthread_mutex_lock(&live_lock);
		assert(queued == DEFERRED_MAX);
		pthread_mutex_unlock(&live_lock);
		release_writer();
		playhistory_flush();
		assert(inserts == DEFERRED_MAX + 1); // In-flight first save plus the bounded retained tail.
	} else if (!strcmp(argv[1], "refusal")) {
		refuse_start = true;
		listen("/card/a.flac");
		assert(!writer_started && inserts == 0 && playhistory_current_id() < 0 && playhistory_unsaved());
		playhistory_flush();
		assert(inserts == 0); // No inline fallback on shutdown/read either.
		refuse_start = false;
		pthread_mutex_lock(&live_lock);
		retry_at = 0;
		pthread_mutex_unlock(&live_lock);
		playhistory_flush();
		assert(inserts == 1);
	} else if (!strcmp(argv[1], "failure")) {
		failed = 1;
		listen("/card/a.flac");
		playhistory_flush();
		assert(inserts == 0 && playhistory_unsaved());
		library_history_item_t *items = NULL;
		assert(playhistory_items(&items) == 1 && items[0].id < 0);
		free(items);
		failed = 0;
		pthread_mutex_lock(&live_lock);
		retry_at = 0;
		pthread_mutex_unlock(&live_lock);
		playhistory_flush();
		assert(inserts == 1 && !playhistory_unsaved());
	} else
		assert(false);
	playhistory_shutdown();
	printf("PASS history async %s\n", argv[1]);
}
