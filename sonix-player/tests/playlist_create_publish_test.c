// Actual library body, actual SQLite, and single ordinary SQL-return failures.
#include "src/system/db/sqlite3.h"
static int fixture_exec(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
#define sqlite3_exec fixture_exec
#include "src/system/library/library.c"
#undef sqlite3_exec
#include <assert.h>

static const char *fail_sql;
static int fail_code;
static bool recording;
static int renames, inserts, creates, drops;
static int fixture_exec(sqlite3 *handle, const char *sql, int (*callback)(void *, int, char **, char **), void *context, char **error) {
	if (recording) {
		renames += strstr(sql, "ALTER TABLE " PLAYLIST_STAGE_TABLE " RENAME TO ") != NULL;
		inserts += strstr(sql, "INSERT INTO ") != NULL;
		creates += strstr(sql, "CREATE TABLE ") != NULL;
		drops += strstr(sql, "DROP TABLE ") != NULL;
	}
	if (fail_sql && strstr(sql, fail_sql)) {
		fail_sql = NULL;
		return fail_code;
	}
	return sqlite3_exec(handle, sql, callback, context, error);
}
static int scalar(const char *text) {
	sqlite3_stmt *statement = NULL;
	assert(sqlite3_prepare_v2(db, text, -1, &statement, NULL) == SQLITE_OK);
	assert(sqlite3_step(statement) == SQLITE_ROW);
	int result = sqlite3_column_int(statement, 0);
	assert(sqlite3_finalize(statement) == SQLITE_OK);
	return result;
}
static void reset_trace(void) { renames = inserts = creates = drops = 0; recording = true; }
static library_playlist_stage_t *seed(int count, bool importer) {
	library_playlist_stage_t *stage = importer ? library_playlist_stage_begin_import(library_database_epoch(), generation[GEN_PLAYLISTS]) : library_playlist_stage_begin(library_database_epoch(), NULL);
	assert(stage);
	for (int i = 0; i < count; i++) {
		library_playlist_row_t row = {.seconds = 100 + i, .present = true};
		snprintf(row.path, sizeof(row.path), "/fixture/%05d.flac", i);
		snprintf(row.title, sizeof(row.title), "Title %05d", i);
		strcpy(row.artist, "Artist");
		assert(importer ? library_playlist_stage_import_rows(stage, &row, 1) : library_playlist_stage_rows(stage, &row, 1));
	}
	return stage;
}
static void stage_retained(library_playlist_stage_t *stage, const char *target, unsigned revision, int count) {
	assert(sqlite3_get_autocommit(db));
	assert(playlist_stage_current(stage) && library_playlist_stage_busy());
	assert(generation[GEN_PLAYLISTS] == revision && stage->rows == count);
	assert(scalar("SELECT COUNT(*) FROM SONIX_QUEUE_STAGE") == count);
	assert(!library_playlist_exists(target));
}
static void open_db(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	for (unsigned i = 0; i < sizeof(SCHEMA) / sizeof(*SCHEMA); i++)
		assert(sqlite3_exec(db, SCHEMA[i], NULL, NULL, NULL) == SQLITE_OK);
	bump_all_generations();
}
static void close_db(void) {
	assert(!library_playlist_stage_busy() && sqlite3_get_autocommit(db));
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
}
int main(void) {
	open_db();
	library_playlist_stage_t *stage = seed(20000, true);
	int rootpage = scalar("SELECT rootpage FROM sqlite_master WHERE name='SONIX_QUEUE_STAGE'");
	unsigned revision = generation[GEN_PLAYLISTS];
	int changes = sqlite3_total_changes(db);
	reset_trace();
	assert(library_playlist_stage_publish(stage, "Large", LIBRARY_PLAYLIST_CREATE, NULL));
	recording = false;
	assert(renames == 1 && inserts == 0 && creates == 0 && drops == 0);
	assert(sqlite3_total_changes(db) == changes);
	assert(scalar("SELECT rootpage FROM sqlite_master WHERE name='M3U_Large'") == rootpage);
	assert(generation[GEN_PLAYLISTS] == revision + 1 && !library_playlist_stage_busy());
	assert(scalar("SELECT COUNT(*) FROM M3U_Large") == 20000);
	assert(scalar("SELECT COUNT(*) FROM M3U_Large WHERE idx BETWEEN 1 AND 20000 AND seconds=99+idx AND mount IS NULL") == 20000);
	library_playlist_row_t row;
	assert(library_playlist_page("Large", 19999, 1, &row) == 1 && !strcmp(row.path, "/fixture/19999.flac") && row.seconds == 20099);
	library_playlist_stage_discard(stage);
	puts("PASS actual CREATE 20k: zero INSERT/CREATE/DROP, zero changed rows, same rootpage/order/metadata/generation");

	assert(library_playlist_create("Schema"));
	sqlite3_stmt *left = NULL, *right = NULL;
	assert(sqlite3_prepare_v2(db, "PRAGMA table_info(M3U_Schema)", -1, &left, NULL) == SQLITE_OK);
	assert(sqlite3_prepare_v2(db, "PRAGMA table_info(M3U_Large)", -1, &right, NULL) == SQLITE_OK);
	for (int i = 0; i < 7; i++) {
		assert(sqlite3_step(left) == SQLITE_ROW && sqlite3_step(right) == SQLITE_ROW);
		for (int column = 0; column < 6; column++) {
			const unsigned char *a = sqlite3_column_text(left, column), *b = sqlite3_column_text(right, column);
			assert((!a && !b) || (a && b && !strcmp((const char *)a, (const char *)b)));
		}
	}
	assert(sqlite3_step(left) == SQLITE_DONE && sqlite3_step(right) == SQLITE_DONE);
	assert(sqlite3_finalize(left) == SQLITE_OK && sqlite3_finalize(right) == SQLITE_OK);
	puts("PASS renamed schema matches all seven legacy columns/defaults/primary keys");

	const char *faults[] = {"ALTER TABLE", "ALTER TABLE", "RELEASE queue_stage_publish", "RELEASE queue_stage_publish"};
	const int codes[] = {SQLITE_IOERR, SQLITE_FULL, SQLITE_IOERR, SQLITE_FULL};
	for (int i = 0; i < 4; i++) {
		stage = seed(2, true);
		revision = generation[GEN_PLAYLISTS];
		fail_sql = faults[i]; fail_code = codes[i];
		library_export_receipt_t receipt = {.valid = true};
		assert(!library_playlist_stage_publish_receipt(stage, "Retry", LIBRARY_PLAYLIST_CREATE, NULL, &receipt));
		assert(!fail_sql && !receipt.valid);
		stage_retained(stage, "Retry", revision, 2);
		assert(library_playlist_stage_publish(stage, "Retry", LIBRARY_PLAYLIST_CREATE, NULL));
		assert(generation[GEN_PLAYLISTS] == revision + 1 && scalar("SELECT COUNT(*) FROM M3U_Retry") == 2);
		library_playlist_stage_discard(stage);
		assert(library_playlist_drop("Retry"));
	}
	puts("PASS rename/RELEASE IOERR and FULL: target absent, stage ownership/rows/revision retained, receipt invalid, retry commits once");
	stage = seed(1, true);
	revision = generation[GEN_PLAYLISTS];
	fail_sql = "ALTER TABLE"; fail_code = SQLITE_FULL;
	assert(!library_playlist_stage_publish(stage, "Cancelled", LIBRARY_PLAYLIST_CREATE, NULL));
	stage_retained(stage, "Cancelled", revision, 1);
	library_playlist_stage_discard(stage);
	assert(!library_playlist_stage_busy() && !scalar("SELECT COUNT(*) FROM sqlite_master WHERE name='SONIX_QUEUE_STAGE'") && generation[GEN_PLAYLISTS] == revision);
	puts("PASS failed CREATE discard removes only its retained stage and does not publish/bump");

	stage = seed(0, true);
	assert(!library_playlist_stage_publish(stage, "Empty", LIBRARY_PLAYLIST_APPEND, NULL));
	assert(!library_playlist_stage_publish(stage, "Empty", LIBRARY_PLAYLIST_REPLACE, NULL));
	assert(library_playlist_stage_publish(stage, "Empty", LIBRARY_PLAYLIST_CREATE, NULL));
	assert(library_playlist_exists("Empty") && scalar("SELECT COUNT(*) FROM M3U_Empty") == 0);
	library_playlist_stage_discard(stage);
	stage = seed(0, false);
	assert(!library_playlist_stage_publish(stage, "Empty queue", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(stage);
	puts("PASS empty import CREATE succeeds; empty APPEND/REPLACE and queue CREATE remain rejected");

	stage = seed(1, true);
	assert(library_playlist_stage_publish(stage, "A\"B's", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(stage);
	stage = seed(1, true);
	revision = generation[GEN_PLAYLISTS];
	assert(!library_playlist_stage_publish(stage, "a\"b'S", LIBRARY_PLAYLIST_CREATE, NULL));
	assert(generation[GEN_PLAYLISTS] == revision && library_playlist_count("A\"B's") == 1);
	library_playlist_stage_discard(stage);
	stage = seed(1, true);
	assert(library_playlist_create("Namespace edit"));
	assert(!library_playlist_stage_publish(stage, "Stale namespace", LIBRARY_PLAYLIST_CREATE, NULL));
	assert(!library_playlist_exists("Stale namespace"));
	library_playlist_stage_discard(stage);
	puts("PASS quoted names/case alias refusal and stale namespace still preserve target isolation");

	stage = seed(3, true);
	reset_trace(); changes = sqlite3_total_changes(db);
	assert(library_playlist_stage_publish(stage, "Large", LIBRARY_PLAYLIST_APPEND, NULL));
	recording = false;
	assert(renames == 0 && inserts == 1 && creates == 1 && drops == 1 && sqlite3_total_changes(db) == changes + 3);
	assert(library_playlist_count("Large") == 20003);
	library_playlist_stage_discard(stage);
	stage = seed(2, true);
	reset_trace();
	assert(library_playlist_stage_publish(stage, "Large", LIBRARY_PLAYLIST_REPLACE, NULL));
	recording = false;
	assert(renames == 1 && inserts == 0 && drops == 1 && creates == 0 && library_playlist_count("Large") == 2);
	library_playlist_stage_discard(stage);
	puts("PASS APPEND retains copy/append and REPLACE retains drop/rename semantics");

	library_export_source_t source = {0};
	source.spec.kind = LIBRARY_LIST_PLAYLIST;
	source.spec.valid = true;
	strcpy(source.spec.value, "Large");
	source.database_epoch = database_epoch;
	source.generation = generation[GEN_PLAYLISTS];
	stage = library_playlist_stage_begin(library_database_epoch(), &source);
	assert(stage);
	library_playlist_row_t fresh = {.seconds = 10, .present = true};
	strcpy(fresh.path, "receipt-row");
	assert(library_playlist_stage_rows(stage, &fresh, 1));
	library_export_receipt_t receipt;
	assert(library_playlist_stage_publish_receipt(stage, "Receipt", LIBRARY_PLAYLIST_CREATE, &source, &receipt));
	assert(receipt.valid && receipt.new_generation == generation[GEN_PLAYLISTS] && receipt.source.generation == source.generation && receipt.source.database_epoch == source.database_epoch && !strcmp(receipt.source.spec.value, "Large"));
	library_playlist_stage_discard(stage);
	puts("PASS CREATE receipt preserves unchanged source identity and committed revision");

	stage = seed(1, true);
	unsigned stale_epoch = database_epoch;
	playlist_writer_close_locked();
	assert(sqlite3_close(db) == SQLITE_OK); db = NULL;
	open_db();
	assert(database_epoch != stale_epoch);
	library_playlist_stage_t *current = seed(1, true);
	revision = generation[GEN_PLAYLISTS];
	assert(!library_playlist_stage_publish(stage, "Old owner", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(stage);
	stage_retained(current, "Old owner", revision, 1);
	assert(library_playlist_stage_publish(current, "Current", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(current);
	close_db();
	puts("PASS old DB epoch cannot publish or discard the next owner's stage");
	return 0;
}
