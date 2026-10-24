// Actual bootstrap, SQLite and history APIs. Run through the Python helper.
#define main original_confirmed_history_fixture_main
#include "playhistory_confirmed_test.c"
#undef main
#include "library_alpha10_schema.inc"

static char fixture_path[1024];

static void close_fixture(void) {
	atomic_store(&database_online, false);
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
}

static void legacy_fixture(const char *directory, const char *name, int tracks) {
	snprintf(fixture_path, sizeof(fixture_path), "%s/%s.db", directory, name);
	assert(access(fixture_path, F_OK) != 0);
	assert(sqlite3_open(fixture_path, &db) == SQLITE_OK);
	assert(exec("BEGIN"));
	for (size_t i = 0; i < sizeof(LEGACY_ALPHA10_SCHEMA) / sizeof(LEGACY_ALPHA10_SCHEMA[0]); i++)
		assert(exec(LEGACY_ALPHA10_SCHEMA[i]));
	for (int i = 0; i < tracks; i++) {
		char sql[768];
		snprintf(sql, sizeof(sql),
				 "INSERT INTO MEDIA_TABLE(id,path,cue_id,name,artist) VALUES(%d,'/fixture/legacy-%03d.flac',0,'Legacy %03d','Artist');"
				 "INSERT INTO PLAY_COUNTS(path,plays,last_played) VALUES('/fixture/legacy-%03d.flac',%d,%d)",
				 i + 1, i, i, i, i + 10, i + 100);
		assert(exec(sql));
	}
	assert(exec("INSERT INTO PLAY_COUNTS(path,plays,last_played) VALUES('/fixture/not-indexed.flac',91,99999)"));
	assert(exec("INSERT INTO FAVOURITES(path,name,artist) VALUES('/fixture/favourite.flac','Keep me','Artist')"));
	assert(exec("INSERT INTO PLAYBACK_STATE(id,path,position) VALUES(0,'/fixture/queue.flac',17.5)"));
	assert(exec("COMMIT"));
}

static void open_current(void) {
	assert(db_open_file(fixture_path));
	atomic_store(&database_online, true);
	atomic_store(&database_epoch, 10);
}

static int scalar(const char *sql) {
	sqlite3_stmt *stmt = NULL;
	assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
	assert(sqlite3_step(stmt) == SQLITE_ROW);
	int value = sqlite3_column_int(stmt, 0);
	assert(sqlite3_finalize(stmt) == SQLITE_OK);
	return value;
}

static void untouched(int tracks) {
	assert(scalar("SELECT COUNT(*) FROM PLAY_COUNTS") == tracks + 1);
	assert(scalar("SELECT plays FROM PLAY_COUNTS WHERE path='/fixture/not-indexed.flac'") == 91);
	assert(scalar("SELECT COUNT(*) FROM FAVOURITES WHERE name='Keep me'") == 1);
	assert(scalar("SELECT position*2 FROM PLAYBACK_STATE WHERE id=0") == 35);
}

static int snapshot(void) {
	library_history_item_t *rows = NULL;
	int count = library_history_items(&rows);
	free(rows);
	return count;
}

int main(int argc, char **argv) {
	assert(argc == 2);
	legacy_fixture(argv[1], "basic", 2);
	close_fixture();
	open_current();
	assert(snapshot() == 2);
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT") == 1);
	assert(scalar("SELECT played_at FROM PLAY_HISTORY ORDER BY id LIMIT 1") == 100);
	assert(scalar("SELECT played_at FROM PLAY_HISTORY ORDER BY id DESC LIMIT 1") == 101);
	untouched(2);
	int changes = sqlite3_total_changes(db);
	assert(history_import_legacy() && snapshot() == 2);
	assert(sqlite3_total_changes(db) == changes);
	assert(exec("ALTER TABLE PLAY_COUNTS RENAME TO fixture_old_counts"));
	assert(history_import_legacy()); // The completed migration never reads old counters again.
	assert(exec("ALTER TABLE fixture_old_counts RENAME TO PLAY_COUNTS"));
	close_fixture();
	open_current();
	assert(snapshot() == 2);
	assert(exec("DELETE FROM PLAY_HISTORY"));
	changes = sqlite3_total_changes(db);
	assert(history_import_legacy() && snapshot() == 0);
	assert(sqlite3_total_changes(db) == changes);
	close_fixture();
	open_current();
	assert(snapshot() == 0); // A cleared, already upgraded history stays empty.
	untouched(2);
	close_fixture();

	legacy_fixture(argv[1], "bounded", 150);
	close_fixture();
	open_current();
	assert(snapshot() == LIBRARY_RECENT_MAX);
	assert(scalar("SELECT played_at FROM PLAY_HISTORY ORDER BY id LIMIT 1") == 150);
	assert(scalar("SELECT played_at FROM PLAY_HISTORY ORDER BY id DESC LIMIT 1") == 249);
	untouched(150);
	close_fixture();

	legacy_fixture(argv[1], "populated", 2);
	assert(exec("CREATE TABLE PLAY_HISTORY(id INTEGER PRIMARY KEY AUTOINCREMENT,path TEXT,name TEXT,artist TEXT,played_at INT)"));
	assert(exec("INSERT INTO PLAY_HISTORY VALUES(42,'/fixture/legacy-000.flac','Recorded title','Recorded artist',70)"));
	close_fixture();
	open_current();
	assert(snapshot() == 1);
	assert(scalar("SELECT id FROM PLAY_HISTORY") == 42);
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY WHERE name='Recorded title' AND artist='Recorded artist'") == 1);
	untouched(2);
	close_fixture();

	legacy_fixture(argv[1], "failure", 2);
	close_fixture();
	open_current();
	assert(exec("DELETE FROM PLAY_HISTORY; DELETE FROM PLAY_HISTORY_IMPORT"));
	assert(exec("CREATE TRIGGER fail_legacy BEFORE INSERT ON PLAY_HISTORY WHEN NEW.path='/fixture/legacy-001.flac' BEGIN SELECT RAISE(ABORT,'fixture interruption'); END"));
	assert(!history_import_legacy());
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY") == 0);
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT") == 0);
	untouched(2);
	assert(exec("DROP TRIGGER fail_legacy"));
	assert(history_import_legacy() && snapshot() == 2);
	assert(exec("DELETE FROM PLAY_HISTORY; DELETE FROM PLAY_HISTORY_IMPORT"));
	assert(exec("BEGIN"));
	assert(!history_import_legacy() && !sqlite3_get_autocommit(db));
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT") == 0);
	assert(exec("ROLLBACK"));
	assert(history_import_legacy() && snapshot() == 2);
	close_fixture();

	legacy_fixture(argv[1], "restored-index", 1);
	assert(exec("DELETE FROM MEDIA_TABLE"));
	close_fixture();
	open_current();
	assert(snapshot() == 0 && scalar("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT") == 0);
	assert(exec("INSERT INTO MEDIA_TABLE(id,path,cue_id,name,artist) VALUES(1,'/fixture/legacy-000.flac',0,'Restored title','Artist')"));
	atomic_store(&database_online, false);
	assert(snapshot() == 0); // No migration write after local database ownership ends.
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY_IMPORT") == 0);
	atomic_store(&database_online, true);
	assert(snapshot() == 1);
	assert(scalar("SELECT COUNT(*) FROM PLAY_HISTORY WHERE name='Restored title'") == 1);
	untouched(1);
	close_fixture();

	bool kept = false;
	for (size_t i = 0; i < sizeof(KEPT_TABLES) / sizeof(KEPT_TABLES[0]); i++)
		kept |= strcmp(KEPT_TABLES[i], "PLAY_HISTORY_IMPORT") == 0;
	assert(kept);
	puts("PASS actual alpha10 bootstrap, bounded order, existing history, clear/reopen, interrupted import, foreign transaction, restored index, offline guard and unchanged user data");
	return 0;
}
