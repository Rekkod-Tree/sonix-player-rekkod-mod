#define _GNU_SOURCE 1
#include "src/system/db/sqlite3.h"
#include <stdbool.h>
static int checked_exec(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *,
						char **);
#define sqlite3_exec checked_exec
#include "src/system/library/library.c"
#undef sqlite3_exec
#include "src/system/library/playlists.h"
#include <assert.h>
#include <stdatomic.h>
static int batches, publishes, creates;
static const char *fail_sql;
static bool fail_import_alloc;
static bool inject_read_error = true;
static int bad_reads;
static int hook;
static library_playlist_stage_t *new_card_stage;
static int checked_exec(sqlite3 *d, const char *s, int (*cb)(void *, int, char **, char **),
						void *u, char **e) {
	if (fail_sql && strstr(s, fail_sql)) {
		fail_sql = NULL;
		return SQLITE_IOERR;
	}
	int rc = sqlite3_exec(d, s, cb, u, e);
	if (rc == SQLITE_OK) {
		if (!strcmp(s, "RELEASE queue_stage_batch"))
			batches++;
		if (!strcmp(s, "RELEASE queue_stage_publish"))
			publishes++;
		if (strstr(s, "CREATE TABLE SONIX_QUEUE_STAGE"))
			creates++;
	}
	return rc;
}
static void sql(const char *s) { assert(sqlite3_exec(db, s, NULL, NULL, NULL) == SQLITE_OK); }
static void open_db(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	for (unsigned i = 0; i < sizeof(SCHEMA) / sizeof(*SCHEMA); i++)
		sql(SCHEMA[i]);
	bump_all_generations();
}
static library_playlist_row_t row(const char *p) {
	library_playlist_row_t r = {.seconds = 77, .present = true};
	snprintf(r.path, sizeof(r.path), "%s", p);
	strcpy(r.title, "Old title");
	strcpy(r.artist, "Old artist");
	return r;
}
static void target(const char *name) {
	library_playlist_row_t r = row("old");
	if (library_playlist_exists(name))
		assert(library_playlist_drop(name));
	assert(library_playlist_create(name) && library_playlist_append(name, &r));
}
static void expect_old(const char *name) {
	library_playlist_row_t r;
	assert(library_playlist_count(name) == 1 && library_playlist_page(name, 0, 1, &r) == 1 &&
		   !strcmp(r.path, "old") && !strcmp(r.title, "Old title") && r.seconds == 77);
}
void *fixture_calloc(size_t n, size_t s) {
	if (fail_import_alloc && n == LIBRARY_PLAYLIST_IMPORT_BATCH &&
		s == sizeof(library_playlist_row_t)) {
		fail_import_alloc = false;
		return NULL;
	}
	return calloc(n, s);
}
static ssize_t bad_read(void *cookie, char *buffer, size_t n) {
	const char **p = cookie;
	if (!**p) {
		errno = EIO;
		return -1;
	}
	size_t left = strlen(*p);
	if (n > left)
		n = left;
	memcpy(buffer, *p, n);
	*p += n;
	return (ssize_t)n;
}
static int bad_close(void *cookie) {
	free(cookie);
	return 0;
}
FILE *fixture_fopen(const char *p, const char *m) {
	if (inject_read_error && strstr(p, "ReadError.m3u") && !strcmp(m, "r")) {
		bad_reads++;
		const char **c = malloc(sizeof(*c));
		assert(c);
		*c = "#EXTM3U\n#[Sonix Playlist]\na.flac\n";
		return fopencookie(c, "r", (cookie_io_functions_t){.read = bad_read, .close = bad_close});
	}
	return fopen(p, m);
}
int cue_split_path(const char *p, char *out, size_t n) {
	(void)p;
	(void)out;
	(void)n;
	return 0;
}
bool playlist_is_playable_file(const char *p) { return strstr(p, ".flac") != NULL; }
static void *concurrent_backfill(void *unused) {
	(void)unused;
	pthread_mutex_lock(&db_lock);
	assert(sqlite3_get_autocommit(db));
	sql("BEGIN");
	sql("INSERT INTO MEDIA_TABLE(id,path,name,artist) VALUES(99,'backfill','Backfill','Worker')");
	sql("COMMIT");
	pthread_mutex_unlock(&db_lock);
	return NULL;
}
void metadata_read(const char *p, song_metadata_t *out) {
	(void)p;
	memset(out, 0, sizeof(*out));
	strcpy(out->title, "Fixture title");
	strcpy(out->artist, "Fixture artist");
	int action = hook;
	hook = 0;
	if (action == 1) {
		pthread_t other;
		assert(!pthread_create(&other, NULL, concurrent_backfill, NULL) &&
			   !pthread_join(other, NULL));
	} else if (action == 2) {
		library_playlist_row_t r = row("foreign");
		assert(library_playlist_create("Other") && library_playlist_append("Other", &r));
	} else if (action == 3) {
		pthread_mutex_lock(&db_lock);
		assert(sqlite3_get_autocommit(db));
		sql("BEGIN");
		sql("CREATE TABLE ForeignTX(n)");
		sql("INSERT INTO ForeignTX VALUES(9)");
		pthread_mutex_unlock(&db_lock);
	} else if (action == 4) {
		pthread_mutex_lock(&db_lock);
		playlist_writer_close_locked();
		assert(sqlite3_close(db) == SQLITE_OK);
		db = NULL;
		bump_all_generations();
		pthread_mutex_unlock(&db_lock);
		open_db();
		target("Mix");
		new_card_stage = library_playlist_stage_begin_import(
			library_database_epoch(), library_revision(LIBRARY_LIST_PLAYLIST));
		library_playlist_row_t r = row("new-card");
		assert(new_card_stage && library_playlist_stage_import_rows(new_card_stage, &r, 1));
	}
}
static void m3u(const char *p, const char *a, const char *b, int count) {
	FILE *f = fopen(p, "w");
	assert(f);
	fputs("#EXTM3U\n", f);
	for (int i = 0; i < count; i++)
		fprintf(f, "#EXTINF:61,Entry%d\n%s\n", i, i % 2 ? b : a);
	assert(!fclose(f));
}
extern int fixture_rows_from_file(const char *, const char *);
static bool ignore_name(const char *name, void *user) {
	(void)name;
	(void)user;
	return true;
}
int main(void) {
	open_db();
	char dir[] = "/tmp/sonix-atomic-import-XXXXXX", a[512], b[512], source[512];
	assert(mkdtemp(dir));
	snprintf(a, sizeof(a), "%s/a.flac", dir);
	snprintf(b, sizeof(b), "%s/b.flac", dir);
	FILE *f = fopen(a, "w");
	assert(f && !fclose(f));
	f = fopen(b, "w");
	assert(f && !fclose(f));
	playlists_init(dir);
	snprintf(source, sizeof(source), "%s/Mix.m3u", dir);
	m3u(source, a, b, 20000);
	target("Mix");
	target("Mix (import)");
	unsigned gen = library_revision(LIBRARY_LIST_PLAYLIST);
	batches = publishes = creates = 0;
	uint32_t start = now_ms();
	playlists_import_result_t result;
	assert(playlists_import(source, true, &result));
	assert(!strcmp(result.name, "Mix") && result.total == 20000 && result.found == 20000);
	assert(library_playlist_count("mix") == 20000 &&
		   library_revision(LIBRARY_LIST_PLAYLIST) == gen + 1);
	assert(batches == 79 && publishes == 1 && creates == 1);
	library_playlist_row_t r;
	assert(library_playlist_page("Mix", 0, 1, &r) == 1 && !strcmp(r.path, a));
	assert(library_playlist_page("Mix", 19999, 1, &r) == 1 && !strcmp(r.path, b));
	expect_old("Mix (import)");
	printf("PASS real importer20k:79 bounded batches,1 publish,1 stage create; heap=%zu "
		   "bytes;host_ms=%u (not SD latency)\n",
		   LIBRARY_PLAYLIST_IMPORT_BATCH * sizeof(r), now_ms() - start);
	m3u(source, a, b, 600);
	for (int failure = 0; failure < 3; failure++) {
		target("Mix");
		gen = library_revision(LIBRARY_LIST_PLAYLIST);
		if (failure == 0)
			fail_import_alloc = true;
		else
			fail_sql = failure == 1 ? "ALTER TABLE" : "RELEASE queue_stage_publish";
		assert(!playlists_import(source, true, &result));
		expect_old("Mix");
		expect_old("Mix (import)");
		assert(library_revision(LIBRARY_LIST_PLAYLIST) == gen && !library_playlist_stage_busy() &&
			   sqlite3_get_autocommit(db));
	}
	puts("PASS allocation/rename/final-commit failures preserve original target, unrelated "
		 "import-named playlist and generation");
	target("Mix");
	hook = 1;
	assert(playlists_import(source, true, &result) && library_playlist_count("Mix") == 600);
	puts("PASS scan/backfill-shaped ordinary transaction between metadata and stage batches does "
		 "not join importer TX");
	target("Mix");
	hook = 2;
	assert(!playlists_import(source, true, &result));
	expect_old("Mix");
	assert(library_playlist_count("Other") == 1 && !library_playlist_stage_busy());
	puts("PASS concurrent playlist edit invalidates target namespace, not foreign committed data");
	target("Mix");
	gen = library_revision(LIBRARY_LIST_PLAYLIST);
	hook = 3;
	assert(!playlists_import(source, true, &result));
	expect_old("Mix");
	assert(!sqlite3_get_autocommit(db) && library_revision(LIBRARY_LIST_PLAYLIST) == gen);
	sql("COMMIT");
	assert(sqlite3_get_autocommit(db));
	puts("PASS foreign transaction that starts during metadata keeps ownership on import abort");
	target("ReadError");
	char bad[512];
	snprintf(bad, sizeof(bad), "%s/ReadError.m3u", dir);
	gen = library_revision(LIBRARY_LIST_PLAYLIST);
	assert(!playlists_import(bad, true, &result));
	expect_old("ReadError");
	assert(library_revision(LIBRARY_LIST_PLAYLIST) == gen);
	assert(fixture_rows_from_file("Bad migration", bad) == -1 &&
		   !library_playlist_exists("Bad migration"));
	puts("PASS actual ferror after a valid first line prevents truncated import and one-time "
		 "migration");
	snprintf(source, sizeof(source), "%s/Migrate.m3u", dir);
	m3u(source, a, b, 513);
	assert(fixture_rows_from_file("Migration", source) == 513 &&
		   library_playlist_count("Migration") == 513);
	puts("PASS existing marked-M3U row-reader uses same bounded staging without long TX");
	char folder[512], backup[512], empty[512];
	snprintf(folder, sizeof(folder), "%s/Playlist", dir);
	assert(mkdir(folder, 0700) == 0);
	snprintf(backup, sizeof(backup), "%s/Playlist/ReadError.m3u", dir);
	f = fopen(backup, "w");
	assert(f);
	fprintf(f, "#EXTM3U\n#[Sonix Playlist]\n%s\n", a);
	assert(!fclose(f));
	snprintf(empty, sizeof(empty), "%s/Playlist/Empty.m3u", dir);
	f = fopen(empty, "w");
	assert(f);
	fputs("#EXTM3U\n#[Sonix Playlist]\n", f);
	assert(!fclose(f));
	assert(library_playlist_drop("ReadError"));
	playlists_init(dir);
	playlists_for_each(ignore_name, NULL);
	assert(!library_playlists_migrated() && !library_playlist_exists("ReadError"));
	int reads = bad_reads;
	playlists_for_each(ignore_name, NULL);
	assert(bad_reads == reads);
	inject_read_error = false;
	playlists_for_each(ignore_name, NULL);
	assert(!library_playlists_migrated());
	playlists_init(dir);
	playlists_for_each(ignore_name, NULL);
	assert(library_playlists_migrated() && library_playlist_count("ReadError") == 1 &&
		   library_playlist_exists("Empty") && library_playlist_count("Empty") == 0);
	f = fopen(backup, "r");
	assert(f);
	char preserved[1024], expected[1024];
	size_t saved = fread(preserved, 1, sizeof(preserved) - 1, f);
	assert(!ferror(f) && !fclose(f));
	preserved[saved] = 0;
	snprintf(expected, sizeof(expected), "#EXTM3U\n#[Sonix Playlist]\n%s\n", a);
	assert(!strcmp(preserved, expected) && access(empty, F_OK) == 0);
	puts("PASS actual migration caller defers done after I/O failure, no retry until remount; "
		 "empty valid backup is not an error; original files untouched");
	snprintf(source, sizeof(source), "%s/Mix.m3u", dir);
	hook = 4;
	assert(!playlists_import(source, true, &result));
	expect_old("Mix");
	assert(new_card_stage && library_playlist_stage_busy() &&
		   playlist_stage_active == new_card_stage->serial);
	assert(
		library_playlist_stage_publish(new_card_stage, "New owner", LIBRARY_PLAYLIST_CREATE, NULL));
	library_playlist_stage_discard(new_card_stage);
	new_card_stage = NULL;
	assert(library_playlist_count("New owner") == 1);
	puts("PASS old import across DBepoch change cannot publish, rollback or discard the new "
		 "owner's stage");
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	assert(!unlink(a) && !unlink(b));
	snprintf(source, sizeof(source), "%s/Mix.m3u", dir);
	assert(!unlink(source));
	snprintf(source, sizeof(source), "%s/Migrate.m3u", dir);
	assert(!unlink(source));
	assert(!unlink(backup) && !unlink(empty) && !rmdir(folder));
	assert(!rmdir(dir));
	return 0;
}
