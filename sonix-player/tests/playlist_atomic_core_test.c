// Actual library SQLite code with ordinary return-code failures only.
#include "src/system/db/sqlite3.h"
#include <stdbool.h>
static int checked_exec(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *,
						char **);
static int checked_reset(sqlite3_stmt *);
static int checked_bind_text(sqlite3_stmt *, int, const char *, int, void (*)(void *));
static int checked_step(sqlite3_stmt *);
#define sqlite3_exec checked_exec
#define sqlite3_reset checked_reset
#define sqlite3_bind_text checked_bind_text
#define sqlite3_step checked_step
#include "src/system/library/library.c"
#undef sqlite3_exec
#undef sqlite3_reset
#undef sqlite3_bind_text
#undef sqlite3_step
#include <assert.h>
static bool fail_reset, fail_bind, fail_step;
static const char *fail_sql;
static int checked_exec(sqlite3 *d, const char *s, int (*cb)(void *, int, char **, char **),
						void *u, char **e) {
	if (fail_sql && strstr(s, fail_sql)) {
		fail_sql = NULL;
		return SQLITE_IOERR;
	}
	return sqlite3_exec(d, s, cb, u, e);
}
static int checked_reset(sqlite3_stmt *s) {
	if (fail_reset) {
		fail_reset = false;
		return SQLITE_ERROR;
	}
	return sqlite3_reset(s);
}
static int checked_bind_text(sqlite3_stmt *s, int i, const char *t, int n, void (*f)(void *)) {
	if (fail_bind && i == 1) {
		fail_bind = false;
		return SQLITE_NOMEM;
	}
	return sqlite3_bind_text(s, i, t, n, f);
}
static int checked_step(sqlite3_stmt *s) {
	if (fail_step) {
		fail_step = false;
		return SQLITE_IOERR;
	}
	return sqlite3_step(s);
}
static void sql(const char *s) { assert(sqlite3_exec(db, s, NULL, NULL, NULL) == SQLITE_OK); }
static int total(const char *name) {
	char q[PLAYLIST_TABLE_MAX], s[PLAYLIST_TABLE_MAX + 50];
	assert(playlist_table(name, q, sizeof(q)));
	snprintf(s, sizeof(s), "SELECT COUNT(*) FROM %s", q);
	sqlite3_stmt *t;
	assert(sqlite3_prepare_v2(db, s, -1, &t, NULL) == SQLITE_OK && sqlite3_step(t) == SQLITE_ROW);
	int n = sqlite3_column_int(t, 0);
	sqlite3_finalize(t);
	return n;
}
static void expect_old(const char *name) {
	char q[PLAYLIST_TABLE_MAX], s[PLAYLIST_TABLE_MAX + 80];
	assert(playlist_table(name, q, sizeof(q)));
	snprintf(s, sizeof(s), "SELECT path,title,artist,seconds,mount FROM %s", q);
	sqlite3_stmt *t;
	assert(sqlite3_prepare_v2(db, s, -1, &t, NULL) == SQLITE_OK && sqlite3_step(t) == SQLITE_ROW);
	assert(!strcmp((const char *)sqlite3_column_text(t, 0), "old") &&
		   !strcmp((const char *)sqlite3_column_text(t, 1), "Old title") &&
		   !strcmp((const char *)sqlite3_column_text(t, 2), "Old artist") &&
		   sqlite3_column_int(t, 3) == 77 && sqlite3_column_int(t, 4) == 91);
	assert(sqlite3_step(t) == SQLITE_DONE);
	sqlite3_finalize(t);
}
static void open_db(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	bump_all_generations();
}
int main(void) {
	open_db();
	library_playlist_row_t old = {.seconds = 77, .present = true},
						   fresh = {.seconds = 55, .present = true};
	strcpy(old.path, "old");
	strcpy(old.title, "Old title");
	strcpy(old.artist, "Old artist");
	strcpy(fresh.path, "fresh");
	assert(library_playlist_create("Case") && library_playlist_append("Case", &old));
	sql("UPDATE M3U_Case SET mount=91");
	assert(library_playlist_exists("case") && !library_playlist_create("CASE"));
	unsigned gen = generation[GEN_PLAYLISTS];
	sql("BEGIN");
	sql("CREATE TABLE ForeignData(n)");
	sql("INSERT INTO ForeignData VALUES(1)");
	assert(!library_playlist_write_begin("Case") && !library_playlist_write_begin("NeverCreated"));
	assert(!sqlite3_get_autocommit(db) && !library_playlist_exists("NeverCreated"));
	sql("COMMIT");
	assert(total("Case") == 1 && generation[GEN_PLAYLISTS] == gen);
	puts("PASS foreign TX rejected before table mutation, foreign TX remains caller-owned");
	for (int kind = 0; kind < 5; kind++) {
		gen = generation[GEN_PLAYLISTS];
		library_playlist_writer_t *w = library_playlist_write_begin("Case");
		assert(w);
		if (kind == 0)
			fail_reset = true;
		if (kind == 1)
			fail_bind = true;
		if (kind == 2)
			fail_step = true;
		if (kind < 3)
			assert(!library_playlist_write_row(w, &fresh));
		else
			assert(library_playlist_write_row(w, &fresh));
		if (kind == 3)
			fail_sql = "COMMIT";
		assert(!library_playlist_write_end(w, kind != 4));
		assert(sqlite3_get_autocommit(db) && generation[GEN_PLAYLISTS] == gen);
		expect_old("Case");
	}
	puts("PASS legacy reset/bind/step/commit/Cancel returns preserve old rows+metadata+generation");
	gen = generation[GEN_PLAYLISTS];
	library_playlist_writer_t *invalid = library_playlist_write_begin("Case");
	assert(invalid && library_playlist_write_row(invalid, &fresh));
	assert(!library_playlist_write_row(invalid, NULL));
	assert(!library_playlist_write_end(invalid, true));
	assert(generation[GEN_PLAYLISTS] == gen);
	expect_old("Case");
	puts("PASS invalid row marks legacy writer failed before success can be reported");

	library_playlist_writer_t *w = library_playlist_write_begin("Case");
	assert(w && library_playlist_write_row(w, &fresh) && library_playlist_write_end(w, true));
	assert(total("Case") == 2);
	puts("PASS legacy append keeps existing target behavior");
	library_playlist_row_t pair[2] = {fresh, fresh};
	assert(library_playlist_append_all("Case", pair, 2) && total("Case") == 4);
	sql("BEGIN");
	assert(!library_playlist_append_all("Case", pair, 2) && !sqlite3_get_autocommit(db));
	sql("ROLLBACK");
	assert(total("Case") == 4);
	puts("PASS existing append_all caller appends and refuses a foreign outer transaction");
	assert(library_playlist_append_all("Empty append", &fresh, 0));
	assert(library_playlist_exists("Empty append") && total("Empty append") == 0);
	assert(library_playlist_append_all("Empty append", &fresh, 0));
	library_playlist_row_t *many = calloc(257, sizeof(*many));
	assert(many);
	for (int i = 0; i < 257; i++) {
		many[i] = fresh;
		snprintf(many[i].path, sizeof(many[i].path), "prepared-%d", i);
	}
	assert(library_playlist_append_all("Case", many, 257) && total("Case") == 261);
	free(many);
	puts("PASS prepared append_all spans bounded chunks and preserves zero-row creation/success");

	assert(library_playlist_drop("Case") && library_playlist_create("Case") &&
		   library_playlist_append("Case", &old));
	sql("UPDATE M3U_Case SET mount=91");
	for (int kind = 0; kind < 3; kind++) {
		gen = generation[GEN_PLAYLISTS];
		library_playlist_stage_t *st =
			library_playlist_stage_begin_import(library_database_epoch(), gen);
		assert(st);
		if (kind == 0)
			fail_reset = true;
		if (kind == 1)
			fail_bind = true;
		if (kind == 2)
			fail_step = true;
		assert(!library_playlist_stage_import_rows(st, &fresh, 1));
		assert(!library_playlist_stage_publish(st, "Case", LIBRARY_PLAYLIST_REPLACE, NULL));
		assert(sqlite3_get_autocommit(db) && generation[GEN_PLAYLISTS] == gen);
		expect_old("Case");
		library_playlist_stage_discard(st);
	}
	puts("PASS import-stage reset/bind/step refusal never publishes partial data");
	for (int kind = 0; kind < 2; kind++) {
		gen = generation[GEN_PLAYLISTS];
		library_playlist_stage_t *st =
			library_playlist_stage_begin_import(library_database_epoch(), gen);
		assert(st && library_playlist_stage_import_rows(st, &fresh, 1));
		fail_sql = kind ? "RELEASE queue_stage_publish" : "ALTER TABLE";
		assert(!library_playlist_stage_publish(st, "case", LIBRARY_PLAYLIST_REPLACE, NULL));
		assert(sqlite3_get_autocommit(db) && generation[GEN_PLAYLISTS] == gen);
		expect_old("Case");
		library_playlist_stage_discard(st);
	}
	puts("PASS replace rename/commit refusal preserves canonical target+metadata+generation "
		 "atomically");
	gen = generation[GEN_PLAYLISTS];
	library_playlist_stage_t *st =
		library_playlist_stage_begin_import(library_database_epoch(), gen);
	assert(st && library_playlist_stage_import_rows(st, &fresh, 1));
	assert(!library_playlist_stage_publish(st, "case", LIBRARY_PLAYLIST_CREATE, NULL));
	expect_old("Case");
	library_playlist_stage_discard(st);
	puts("PASS CREATE rejects SQLite case alias, never silently APPENDs");
	for (int cause = 0; cause < 2; cause++) {
		gen = generation[GEN_PLAYLISTS];
		st = library_playlist_stage_begin_import(library_database_epoch(), gen);
		assert(st && library_playlist_stage_import_rows(st, &fresh, 1));
		if (cause == 0)
			fail_bind = true;
		else
			fail_step = true;
		assert(!library_playlist_stage_publish(st, "Case", LIBRARY_PLAYLIST_CREATE, NULL));
		assert(generation[GEN_PLAYLISTS] == gen);
		expect_old("Case");
		library_playlist_stage_discard(st);
	}
	fail_bind = true;
	assert(!library_playlist_create("Lookup unavailable"));
	assert(!library_playlist_exists("Lookup unavailable"));
	fail_step = true;
	assert(!library_playlist_rename("Case", "Lookup renamed"));
	expect_old("Case");
	assert(!library_playlist_exists("Lookup renamed"));
	puts("PASS namespace lookup bind/step failures refuse CREATE/rename before any mutation");

	st = library_playlist_stage_begin_import(library_database_epoch(), generation[GEN_PLAYLISTS]);
	assert(st && library_playlist_stage_import_rows(st, &fresh, 1));
	assert(library_playlist_create("Other"));
	assert(!library_playlist_stage_publish(st, "Case", LIBRARY_PLAYLIST_REPLACE, NULL));
	library_playlist_stage_discard(st);
	expect_old("Case");
	assert(!library_playlist_stage_busy());
	puts("PASS changed namespace rejects old stage and releases its own reservation");
	st = library_playlist_stage_begin(library_database_epoch(), NULL);
	assert(st && !library_playlist_stage_import_rows(st, &fresh, 1));
	assert(!library_playlist_stage_rows(st, &fresh, 17));
	assert(library_playlist_stage_rows(st, &fresh, 1));
	assert(library_playlist_stage_publish(st, "Queue compatible", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(st);
	puts("PASS ordinary queue stage keeps its16-row limit and existing API behavior");
	const char *quoted = "A\"B's";
	st = library_playlist_stage_begin_import(library_database_epoch(), generation[GEN_PLAYLISTS]);
	assert(st && library_playlist_stage_import_rows(st, &fresh, 1) &&
		   library_playlist_stage_publish(st, quoted, LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(st);
	assert(library_playlist_exists("a\"b'S") && total(quoted) == 1);
	puts("PASS quoted identifier and ASCII case aliases remain bound/escaped");
	unsigned stale_epoch = library_database_epoch();
	w = library_playlist_write_begin("Case");
	assert(w && library_playlist_write_row(w, &fresh));
	pthread_mutex_lock(&db_lock);
	playlist_writer_close_locked();
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	bump_all_generations();
	pthread_mutex_unlock(&db_lock);
	open_db();
	sql("CREATE TABLE Sentinel(n)");
	sql("BEGIN");
	sql("INSERT INTO Sentinel VALUES(1)");
	assert(!library_playlist_write_row(w, &fresh) && !library_playlist_write_end(w, true));
	assert(!library_playlists_set_migrated_at(stale_epoch));
	assert(!library_playlists_set_migrated_at(library_database_epoch()));
	assert(!sqlite3_get_autocommit(db));
	sql("COMMIT");
	assert(!playlist_writer_active);
	puts(
		"PASS old writer is revoked before close, never commits/rolls back the new DB transaction");
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	return 0;
}
