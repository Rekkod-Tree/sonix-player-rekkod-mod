// Actual SQLite/library implementation plus the production history component.
#include "src/system/library/library.c"
#include "src/system/playback/playhistory.h"
#include <assert.h>

static bool fake_ready;
static char fake_file[512];
static unsigned fake_serial = 1;
bool clock_is_set(void) { return true; }
bool audio_get_listening_progress(double *position, double *total, unsigned *serial) {
	if (position)
		*position = 10;
	if (total)
		*total = 100;
	if (serial)
		*serial = fake_serial;
	return fake_ready;
}
void audio_get_current_file(char *out, size_t size) { snprintf(out, size, "%s", fake_file); }

static int stored_count(void) {
	sqlite3_stmt *s;
	assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM PLAY_HISTORY", -1, &s, NULL) == SQLITE_OK);
	assert(sqlite3_step(s) == SQLITE_ROW);
	int count = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);
	return count;
}
static void start(const char *path, bool waiting) {
	snprintf(fake_file, sizeof(fake_file), "%s", path);
	fake_ready = false;
	fake_serial++;
	playhistory_started(path, "A title", "Artist", false, waiting);
}
int main(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	assert(exec("CREATE TABLE MEDIA_TABLE(path TEXT, name TEXT)"));
	assert(exec("CREATE TABLE PLAY_HISTORY(id INTEGER PRIMARY KEY AUTOINCREMENT,path TEXT,name TEXT,artist TEXT,played_at INT)"));
	atomic_store(&database_online, true);
	atomic_store(&database_epoch, 10);
	start("/fixture/a.flac", false);
	assert(stored_count() == 0); // A request alone is not a heard event.
	fake_ready = true;
	playhistory_heard(fake_file);
	assert(stored_count() == 1 && playhistory_current_id() > 0);
	long long a = playhistory_current_id();
	playhistory_heard(fake_file);
	playhistory_resumed();
	playhistory_heard(fake_file);
	assert(stored_count() == 1);
	playhistory_metadata(fake_file, "Late title", "Late artist");
	playhistory_poll();
	char name[256];
	assert(playhistory_entry(a, NULL, 0, name, sizeof(name), NULL, 0));
	assert(!strcmp(name, "Late title"));
	playhistory_left();
	assert(stored_count() == 1);
	start("/fixture/b.flac", false);
	playhistory_left();
	assert(stored_count() == 1);
	start("/fixture/restored.flac", true);
	fake_ready = true;
	playhistory_heard(fake_file);
	assert(stored_count() == 1);
	playhistory_resumed();
	playhistory_heard(fake_file);
	assert(stored_count() == 2);
	playhistory_left();
	start("/fixture/short.flac", false);
	fake_ready = true;
	playhistory_left();
	assert(stored_count() == 3); // Heard between GUI polls.
	assert(exec("BEGIN"));
	start("/fixture/deferred.flac", false);
	fake_ready = true;
	playhistory_heard(fake_file);
	assert(stored_count() == 3 && !sqlite3_get_autocommit(db));
	assert(playhistory_current_id() < 0);
	long long deferred_id = playhistory_current_id();
	library_history_item_t *items = NULL;
	int count = playhistory_items(&items);
	assert(count == 4 && items[count - 1].id < 0);
	free(items);
	assert(exec("ROLLBACK"));
	playhistory_poll();
	assert(stored_count() == 4 && playhistory_current_id() > 0);
	assert(playhistory_entry(deferred_id, NULL, 0, name, sizeof(name), NULL, 0));
	playhistory_left();
	assert(playhistory_entry(deferred_id, NULL, 0, name, sizeof(name), NULL, 0));
	assert(exec("BEGIN"));
	start("/fixture/old-card.flac", false);
	fake_ready = true;
	playhistory_heard(fake_file);
	assert(playhistory_current_id() < 0);
	assert(exec("ROLLBACK"));
	atomic_fetch_add(&database_epoch, 1);
	playhistory_poll();
	assert(stored_count() == 4 && playhistory_current_id() == 0);
	assert(!playhistory_entry(deferred_id, NULL, 0, name, sizeof(name), NULL, 0));
	assert(!library_history_entry_at(10, a, NULL, 0, name, sizeof(name), NULL, 0));
	assert(library_history_record_at(10, "/old", "Old", "", 0) == 0);
	assert(stored_count() == 4);
	assert(exec("BEGIN"));
	for (int i = 0; i < 70; i++) {
		char path[128];
		snprintf(path, sizeof(path), "/fixture/pending-%d.flac", i);
		start(path, false);
		fake_ready = true;
		playhistory_heard(fake_file);
		playhistory_left();
	}
	count = playhistory_items(&items);
	assert(count == 4 + 64 && playhistory_unsaved());
	free(items);
	assert(exec("ROLLBACK"));
	for (int i = 0; i < 20; i++)
		playhistory_poll();
	assert(stored_count() == 4 + 64);
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	puts("PASS confirmed starts, same-listen metadata, paused restore, short listen, deferred transaction, stale card and bounded memory");
	return 0;
}
