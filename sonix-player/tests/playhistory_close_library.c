// Actual library close/history code with a file-backed SQLite fixture.
// Linked only into playhistory_close_test; no device or user card is used.
#define _GNU_SOURCE
#include "src/system/library/library.c"
#include <assert.h>

void covertone_detach(void) {}

void history_close_fixture_open(const char *path) {
	assert(!db);
	assert(sqlite3_open(path, &db) == SQLITE_OK);
	for (size_t i = 0; i < sizeof(SCHEMA) / sizeof(SCHEMA[0]); i++) {
		if (strstr(SCHEMA[i], "CREATE TABLE IF NOT EXISTS PLAY_HISTORY(") || strstr(SCHEMA[i], "CREATE TABLE IF NOT EXISTS PLAY_HISTORY_IMPORT(") || strstr(SCHEMA[i], "CREATE TABLE IF NOT EXISTS PLAY_COUNTS(") || strstr(SCHEMA[i], "CREATE TABLE IF NOT EXISTS MEDIA_TABLE("))
			assert(exec(SCHEMA[i]));
	}
	bump_all_generations();
	// Match library_open's publication without starting optional index workers.
	atomic_store(&database_preparing_close, false);
}

int history_close_fixture_imported(void) {
	pthread_mutex_lock(&db_lock);
	int count = count_rows("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT");
	pthread_mutex_unlock(&db_lock);
	return count;
}

int history_close_fixture_rows(void) {
	pthread_mutex_lock(&db_lock);
	int count = count_rows("SELECT COUNT(*) FROM PLAY_HISTORY");
	pthread_mutex_unlock(&db_lock);
	return count;
}

bool history_close_fixture_in_transaction(void) {
	pthread_mutex_lock(&db_lock);
	bool active = db && !sqlite3_get_autocommit(db);
	pthread_mutex_unlock(&db_lock);
	return active;
}

void history_close_fixture_rollback_import(void) {
	pthread_mutex_lock(&db_lock);
	playlist_writer_close_locked();
	pthread_mutex_unlock(&db_lock);
}
