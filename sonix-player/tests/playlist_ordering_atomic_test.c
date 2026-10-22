// Actual production SQL with ordinary, single return-code failures only.
#include "src/system/db/sqlite3.h"
static int fx_prepare(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
static int fx_exec(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
static int fx_bind_int(sqlite3_stmt *, int, int);
static int fx_bind_int64(sqlite3_stmt *, int, sqlite3_int64);
static int fx_bind_text(sqlite3_stmt *, int, const char *, int, void (*)(void *));
static int fx_step(sqlite3_stmt *);
static int fx_reset(sqlite3_stmt *);
static int fx_finalize(sqlite3_stmt *);
#define sqlite3_prepare_v2 fx_prepare
#define sqlite3_exec fx_exec
#define sqlite3_bind_int fx_bind_int
#define sqlite3_bind_int64 fx_bind_int64
#define sqlite3_bind_text fx_bind_text
#define sqlite3_step fx_step
#define sqlite3_reset fx_reset
#define sqlite3_finalize fx_finalize
#include "src/system/library/library.c"
#undef sqlite3_prepare_v2
#undef sqlite3_exec
#undef sqlite3_bind_int
#undef sqlite3_bind_int64
#undef sqlite3_bind_text
#undef sqlite3_step
#undef sqlite3_reset
#undef sqlite3_finalize
#include <assert.h>
enum failure { NONE, PREPARE, BIND, STEP, RESET, FINALIZE, SAVEPOINT, RELEASE };
static enum failure fault;
static const char *match;
static sqlite3_stmt *tracked;
static int occurrence;
static bool hit(enum failure f, sqlite3_stmt *s) {
	if (fault != f || s != tracked)
		return false;
	if (--occurrence > 0)
		return false;
	fault = NONE;
	return true;
}
static int fx_prepare(sqlite3 *d, const char *s, int n, sqlite3_stmt **o, const char **tail) {
	bool selected = match && strstr(s, match);
	if (selected && fault == PREPARE) {
		fault = NONE;
		*o = NULL;
		return SQLITE_ERROR;
	}
	int rc = sqlite3_prepare_v2(d, s, n, o, tail);
	if (selected && rc == SQLITE_OK)
		tracked = *o;
	return rc;
}
static int fx_exec(sqlite3 *d, const char *s, int (*cb)(void *, int, char **, char **), void *u, char **e) {
	if ((fault == SAVEPOINT && strstr(s, "SAVEPOINT playlist_order")) || (fault == RELEASE && strstr(s, "RELEASE playlist_order"))) {
		fault = NONE;
		return SQLITE_IOERR;
	}
	return sqlite3_exec(d, s, cb, u, e);
}
static int fx_bind_int(sqlite3_stmt *s, int i, int v) { return hit(BIND, s) ? SQLITE_ERROR : sqlite3_bind_int(s, i, v); }
static int fx_bind_int64(sqlite3_stmt *s, int i, sqlite3_int64 v) { return hit(BIND, s) ? SQLITE_ERROR : sqlite3_bind_int64(s, i, v); }
static int fx_bind_text(sqlite3_stmt *s, int i, const char *v, int n, void (*f)(void *)) { return hit(BIND, s) ? SQLITE_ERROR : sqlite3_bind_text(s, i, v, n, f); }
static int fx_step(sqlite3_stmt *s) { return hit(STEP, s) ? SQLITE_IOERR : sqlite3_step(s); }
static int fx_reset(sqlite3_stmt *s) {
	int rc = sqlite3_reset(s);
	return hit(RESET, s) ? SQLITE_IOERR : rc;
}
static int fx_finalize(sqlite3_stmt *s) {
	bool failed = hit(FINALIZE, s);
	int rc = sqlite3_finalize(s);
	if (s == tracked)
		tracked = NULL;
	return failed ? SQLITE_IOERR : rc;
}
static void sql(const char *s) { assert(sqlite3_exec(db, s, NULL, NULL, NULL) == SQLITE_OK); }
static int fence_count(void) {
	sqlite3_stmt *q = NULL;
	assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Fence", -1, &q, NULL) == SQLITE_OK);
	assert(sqlite3_step(q) == SQLITE_ROW);
	int n = sqlite3_column_int(q, 0);
	assert(sqlite3_finalize(q) == SQLITE_OK);
	return n;
}
static void snapshot(char *out, size_t cap) {
	sqlite3_stmt *s = NULL;
	assert(sqlite3_prepare_v2(db, "SELECT idx,path,title,artist,seconds,present,mount FROM \"M3U_A\"\"B's\" ORDER BY idx", -1, &s, NULL) == SQLITE_OK);
	size_t at = 0;
	int rc;
	while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
		int n = snprintf(out + at, cap - at, "%lld|%s|%s|%s|%lld|%d|%d;", sqlite3_column_int64(s, 0), sqlite3_column_text(s, 1), sqlite3_column_text(s, 2), sqlite3_column_text(s, 3), sqlite3_column_int64(s, 4), sqlite3_column_int(s, 5), sqlite3_column_int(s, 6));
		assert(n > 0 && (size_t)n < cap - at);
		at += (size_t)n;
	}
	assert(rc == SQLITE_DONE);
	out[at] = 0;
	assert(sqlite3_finalize(s) == SQLITE_OK);
}
static void setup(void) {
	fault = NONE;
	match = NULL;
	tracked = NULL;
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	for (unsigned i = 0; i < sizeof(SCHEMA) / sizeof(*SCHEMA); i++) {
		sql(SCHEMA[i]);
	}
	bump_all_generations();
	assert(library_playlist_create("A\"B's"));
	for (int i = 0; i < 4; i++) {
		library_playlist_row_t r = {.present = true, .seconds = 70 + i};
		snprintf(r.path, sizeof(r.path), "%c", 'A' + i);
		snprintf(r.title, sizeof(r.title), "Title %c", 'A' + i);
		strcpy(r.artist, "Artist");
		assert(library_playlist_append("A\"B's", &r));
	}
	sql("UPDATE \"M3U_A\"\"B's\" SET idx=-idx,mount=91");
	sql("UPDATE \"M3U_A\"\"B's\" SET idx=-idx*2");
	sql("INSERT INTO \"M3U_A\"\"B's\"(idx,path,title,artist,seconds,present,mount) VALUES(3,'H','Hidden','Artist',99,0,91)");
	sql("CREATE TABLE Fence(n)");
}
static int operation(int op) {
	int pos[] = {0, 2, 2, -1, 99};
	return op == 0 ? library_playlist_move("a\"b's", 0, 2) : op == 1 ? library_playlist_remove_path("a\"b's", "A") : library_playlist_remove_positions("a\"b's", pos, 5);
}
static void done(void) {
	assert(sqlite3_get_autocommit(db));
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
}
static void unchanged(const char *before, unsigned g) {
	char after[2048];
	snapshot(after, sizeof(after));
	assert(!strcmp(before, after));
	assert(generation[GEN_PLAYLISTS] == g);
}
int main(void) {
	for (int op = 0; op < 3; op++) {
		for (int commit = 0; commit < 2; commit++) {
			setup();
			char before[2048];
			snapshot(before, sizeof(before));
			unsigned g = generation[GEN_PLAYLISTS];
			sql("BEGIN");
			sql("INSERT INTO Fence VALUES(1)");
			assert(operation(op) == 0);
			assert(!sqlite3_get_autocommit(db));
			unchanged(before, g);
			sql(commit ? "COMMIT" : "ROLLBACK");
			assert(fence_count() == (commit ? 1 : 0));
			unchanged(before, g);
			done();
		}
		for (int f = PREPARE; f <= RELEASE; f++) {
			if (op == 1 && f == RESET)
				continue;
			setup();
			char before[2048];
			snapshot(before, sizeof(before));
			unsigned g = generation[GEN_PLAYLISTS];
			match = op == 0 ? "UPDATE " : "DELETE ";
			fault = f;
			occurrence = 1;
			if ((f == STEP || f == RESET) && op != 1)
				occurrence = 2;
			assert(operation(op) == 0);
			assert(fault == NONE);
			unchanged(before, g);
			done();
		}
		// Read/namespace failures must not become a partial write.
		for (int f = PREPARE; f <= FINALIZE; f++) {
			if (op == 1 && f == RESET)
				continue;
			setup();
			char before[2048];
			snapshot(before, sizeof(before));
			unsigned g = generation[GEN_PLAYLISTS];
			match = op == 1 ? "sqlite_master" : "SELECT p.idx";
			fault = f;
			occurrence = 1;
			if (op == 0 && f == RESET) {
				done();
				continue;
			} // Move reader has one statement, no resets.
			assert(operation(op) == 0);
			assert(fault == NONE);
			unchanged(before, g);
			done();
		}
		setup();
		unsigned g = generation[GEN_PLAYLISTS];
		int n = operation(op);
		assert(n == (op == 2 ? 2 : 1));
		assert(generation[GEN_PLAYLISTS] != g);
		assert(sqlite3_get_autocommit(db));
		library_playlist_row_t rows[8];
		int count = library_playlist_page("A\"B's", 0, 8, rows);
		assert(count == (op == 0 ? 5 : op == 1 ? 4 : 3));
		if (op == 0) {
			assert(!strcmp(rows[0].path, "B") && !strcmp(rows[2].path, "C") && !strcmp(rows[3].path, "A"));
		}
		done();
	}
	setup();
	assert(library_playlist_move("A\"B's", 2, 0));
	library_playlist_row_t moved[8];
	assert(library_playlist_page("A\"B's", 0, 8, moved) == 5);
	assert(!strcmp(moved[0].path, "C") && !strcmp(moved[0].title, "Title C") && !strcmp(moved[1].path, "H") && !strcmp(moved[2].path, "A") && !strcmp(moved[3].path, "B"));
	done();
	setup();
	library_playlist_row_t duplicate = {.present = true};
	strcpy(duplicate.path, "A");
	strcpy(duplicate.title, "Duplicate A");
	assert(library_playlist_append("A\"B's", &duplicate));
	assert(library_playlist_remove_path("A\"B's", "A"));
	assert(library_playlist_page("A\"B's", 0, 8, moved) == 5);
	assert(!strcmp(moved[4].path, "A") && !strcmp(moved[4].title, "Duplicate A"));
	done();
	setup();
	unsigned g = generation[GEN_PLAYLISTS];
	int invalid[] = {-1, 99};
	assert(!library_playlist_move("A\"B's", 0, INT_MAX));
	assert(!library_playlist_move("A\"B's", 0, 0));
	assert(!library_playlist_move("A\"B's", 0, 99));
	assert(library_playlist_remove_positions("A\"B's", invalid, 2) == 0);
	assert(!library_playlist_remove_path("A\"B's", "missing"));
	assert(generation[GEN_PLAYLISTS] == g);
	done();
	puts("PASS ordering/removal owned transactions, foreign TX, partial-write failures, metadata/order/generation");
	return 0;
}
