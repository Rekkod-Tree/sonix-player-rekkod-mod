// Read-only product investigation. Real library SQL/index + playlist code,
// synthetic in-memory SQLite; no card or user metadata is used by this test.
// Link with tests/device_presync_fixture.c, src/system/playback/playlist.c,
// src/system/core/utils.c and the project's SQLite implementation. Use
// -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread -ldl -lm.
#include "src/system/library/library.c"
#include <assert.h>
void queue_test_device_presync(const char *loaded);

const char *config_get_copy(const char *section, const char *key, const char *fallback, char *out, size_t size) {
	(void)section;
	(void)key;
	snprintf(out, size, "%s", fallback);
	return out;
}
const char *config_get(const char *section, const char *key, const char *fallback) {
	(void)section;
	(void)key;
	return fallback;
}
bool config_get_bool(const char *section, const char *key, bool fallback) {
	(void)section;
	(void)key;
	return fallback;
}
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	(void)key;
	return fallback;
}
void config_set_int(const char *section, const char *key, long value) {
	(void)section;
	(void)key;
	(void)value;
}
bool config_save(void) { return true; }

static void runsql(const char *sql) {
	char *error = NULL;
	int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
	if (rc != SQLITE_OK)
		fprintf(stderr, "synthetic SQL error: %s\n", error ? error : "");
	sqlite3_free(error);
	assert(rc == SQLITE_OK);
}
static void add_track(int id, int stamp) {
	char sql[1500];
	snprintf(sql, sizeof(sql), "INSERT INTO MEDIA_TABLE(id,path,name,album,artist,genre,year,dis_id,ck_id,has_child_file,cue_id,sortkey,ctime,album_key,searchkey) VALUES(%d,'/fixture/%06d.flac','Track %06d','Synthetic album','Synthetic artist','Test',2026,1,%d,0,0,'%06d',%d,'Synthetic album','track')", id, id, id, id, id, stamp);
	runsql(sql);
}
static void reset_library(int n) {
	playlist_clear();
	runsql("DELETE FROM MEDIA_TABLE");
	for (int i = 0; i < n; i++)
		add_track(i, 1000 + i);
	bump_generation(GEN_MEDIA);
	playlist_set_mode(PLAYBACK_MODE_NORMAL);
}
static void load(int at) {
	library_index_t *ix = library_index_open(LIBRARY_LIST_TRACKS, LIBRARY_FILTER_NONE, "", LIBRARY_ORDER_ADDED, false);
	assert(ix && library_index_count(ix) == 696 && playlist_load_index(ix, at));
}
static int path_id(const char *path) {
	int id = -1;
	assert(sscanf(path, "/fixture/%d.flac", &id) == 1);
	return id;
}
static int current(void) {
	char path[512];
	assert(playlist_current_path(path, sizeof(path)));
	return path_id(path);
}
static int advance_one(bool previous) {
	char path[512];
	assert(previous ? playlist_prev(path, sizeof(path)) : playlist_next(path, sizeof(path)));
	return path_id(path);
}
static void open_test_db(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	sqlite3_create_collation(db, "listorder", SQLITE_UTF8, NULL, library_collate_listorder);
	for (unsigned i = 0; i < sizeof(SCHEMA) / sizeof(*SCHEMA); i++)
		runsql(SCHEMA[i]);
	runsql("ALTER TABLE MEDIA_TABLE ADD COLUMN album_key TEXT");
	runsql("ALTER TABLE MEDIA_TABLE ADD COLUMN searchkey TEXT");
}
static pthread_barrier_t scan_gate;
static void *finish_scan(void *unused) {
	(void)unused;
	pthread_barrier_wait(&scan_gate);
	pthread_mutex_lock(&db_lock);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	pthread_mutex_unlock(&db_lock);
	pthread_barrier_wait(&scan_gate);
	return NULL;
}
int main(void) {
	unsigned mismatches = 0;
	open_test_db();
	reset_library(696);
	load(99);
	assert(current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, 1000 + i);
	bump_generation(GEN_MEDIA);
	assert(playlist_count() == 696);
	int prev = advance_one(true);
	printf("APPEND_AFTER cached_count=696 refreshed_count=%d prev_id=%d expected=98\n", playlist_count(), prev);
	assert(playlist_count() == 727 && prev == 98);
	reset_library(696);
	load(99);
	assert(current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	prev = advance_one(true);
	printf("INSERT_BEFORE_CACHE_MISS refreshed_count=%d prev_id=%d expected=98 mismatch=%d\n", playlist_count(), prev, prev != 98);
	mismatches += prev != 98;
	reset_library(696);
	load(99);
	assert(current() == 99);
	char path[512];
	assert(playlist_path_at(98, path, sizeof(path)) && path_id(path) == 98);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	prev = advance_one(true);
	printf("INSERT_BEFORE_CACHE_HIT refreshed_count=%d prev_id=%d expected=98 mismatch=%d\n", playlist_count(), prev, prev != 98);
	reset_library(696);
	load(99);
	assert(current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	assert(current() == 99);
	prev = advance_one(true);
	printf("INSERT_BEFORE_EXPLICIT_REFRESH prev_id=%d expected=98 mismatch=%d\n", prev, prev != 98);
	reset_library(696);
	load(99);
	assert(current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	int next = advance_one(false);
	printf("INSERT_BEFORE_NEXT next_id=%d expected=100 mismatch=%d\n", next, next != 100);
	mismatches += next != 100;
	reset_library(696);
	load(99);
	assert(current() == 99);
	runsql("DELETE FROM MEDIA_TABLE WHERE id < 20");
	bump_generation(GEN_MEDIA);
	prev = advance_one(true);
	printf("DELETE_BEFORE prev_id=%d expected=98 mismatch=%d refreshed_count=%d\n", prev, prev != 98, playlist_count());
	mismatches += prev != 98;
	reset_library(696);
	load(99);
	assert(current() == 99);
	runsql("UPDATE MEDIA_TABLE SET ctime=-ctime");
	bump_generation(GEN_MEDIA);
	next = advance_one(false);
	printf("REORDER_SOURCE next_id=%d expected=98 mismatch=%d\n", next, next != 98);
	mismatches += next != 98;
	// The real device-state manual entry path first calls current_path, and
	// re-locates the loaded path if necessary. Characterize that guard explicitly.
	reset_library(696);
	load(99);
	assert(current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	assert(current() == 99);
	assert(playlist_locate("/fixture/000099.flac"));
	queue_test_device_presync("/fixture/000099.flac");
	next = advance_one(false);
	printf("DEVICE_LIKE_PRESYNC next_id=%d expected=100 mismatch=%d\n", next, next != 100);
	reset_library(696);
	load(99);
	assert(current() == 99);
	queue_test_device_presync("/fixture/000099.flac");
	assert(pthread_barrier_init(&scan_gate, NULL, 2) == 0);
	pthread_t scanner;
	assert(pthread_create(&scanner, NULL, finish_scan, NULL) == 0);
	// The existing device-state pre-sync completed; the scanner now invalidates
	// the index before the manual command reaches playlist_next.
	pthread_barrier_wait(&scan_gate);
	pthread_barrier_wait(&scan_gate);
	assert(pthread_join(scanner, NULL) == 0);
	pthread_barrier_destroy(&scan_gate);
	next = advance_one(false);
	printf("DEVICE_PRESYNC_THEN_SCAN next_id=%d expected=100 mismatch=%d\n", next, next != 100);
	mismatches += next != 100;
	// Persisted query restores its declared source, not an explicit filename list.
	reset_library(696);
	load(99);
	assert(current() == 99);
	library_index_spec_t spec;
	assert(playlist_library_spec(&spec));
	library_queue_save_query(&spec, 99, NULL, NULL, 0);
	playlist_clear();
	int restored_at = -1;
	memset(&spec, 0, sizeof(spec));
	assert(library_queue_load_query(&spec, &restored_at) && spec.valid && restored_at == 99);
	library_index_t *restored = library_index_open(spec.kind, spec.filter, spec.value, spec.order, spec.desc);
	assert(restored && playlist_load_index(restored, restored_at) && current() == 99);
	printf("SAVED_QUERY source_kind=%d filter=%d order=%d restored_count=%d current_id=%d\n", spec.kind, spec.filter, spec.order, playlist_count(), current());
	// Explicit paths (M3U consumption shape) do not become all-library queues.
	const char *explicit_paths[] = {"/fixture/000010.flac", "/fixture/000099.flac", "/fixture/000042.flac"};
	playlist_load_paths(explicit_paths, 3, 1);
	assert(current() == 99 && !playlist_is_library_backed());
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	assert(advance_one(true) == 10 && playlist_count() == 3);
	assert(advance_one(false) == 99 && advance_one(false) == 42);
	playlist_snapshot_t *snap = playlist_snapshot_take();
	assert(snap && playlist_snapshot_count(snap) == 3);
	assert(playlist_move(1, -1) == false); // Already played entries cannot reorder.
	assert(playlist_snapshot_restore(snap, "/fixture/000042.flac"));
	assert(current() == 42 && playlist_count() == 3 && !playlist_is_library_backed());
	for (int i = 0; i < 3; i++) {
		assert(playlist_path_at(i, path, sizeof(path)));
		assert(!strcmp(path, explicit_paths[i]));
	}
	printf("EXPLICIT_M3U unchanged_count=3 previous_next_order=PASS undo=PASS\n");
	reset_library(696);
	load(99);
	assert(current() == 99);
	srand(42);
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
	assert(current() == 99 && playlist_count() == 696);
	snap = playlist_snapshot_take();
	assert(snap);
	assert(playlist_snapshot_restore(snap, "/fixture/000099.flac") && current() == 99);
	for (int i = 696; i < 727; i++)
		add_track(i, 1000 + i);
	bump_generation(GEN_MEDIA);
	current();
	assert(playlist_count() == 727);
	for (int i = 0; i < 20; i++) {
		next = advance_one(false);
		assert(next >= 0 && next < 727);
	}
	printf("SHUFFLE query_refresh_count=727 valid_IDs=20 snapshot_undo=PASS\n");
	reset_library(696);
	load(99);
	assert(current() == 99);
	runsql("DELETE FROM MEDIA_TABLE WHERE id=99");
	bump_generation(GEN_MEDIA);
	next = advance_one(false);
	assert(next != 99 && next >= 0 && next < 696 && playlist_count() == 695);
	printf("RAW_CURRENT_REMOVED selected_present=PASS selected_id=%d\n", next);
	runsql("DELETE FROM MEDIA_TABLE");
	bump_generation(GEN_MEDIA);
	assert(!playlist_next(path, sizeof(path)) && playlist_count() == 0);
	assert(!playlist_prev(path, sizeof(path)));
	printf("EMPTY_RESCAN no_target=PASS count=0\n");
	reset_library(696);
	load(99);
	assert(current() == 99);
	// Model the index's detach boundary, not a physical card or the whole
	// storage supervisor: close its database, revoke all index generations.
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	bump_all_generations();
	assert(!playlist_next(path, sizeof(path)) && !playlist_prev(path, sizeof(path)) && playlist_count() == 0);
	open_test_db();
	for (int i = 900000; i < 900020; i++)
		add_track(i, i);
	bump_all_generations();
	assert(!playlist_next(path, sizeof(path)) && playlist_count() == 0);
	printf("DB_DETACH_READ_REATTACH no_old_queue_after_empty_read=PASS\n");
	// Actual device-state pre-sync is decisive for a missing loaded file:
	// the existing refresh policy anchors to the first remaining row; the
	// requested manual/automatic forward step then selects its successor.
	for (int automatic = 0; automatic < 2; automatic++) {
		reset_library(696);
		load(99);
		assert(current() == 99);
		runsql("DELETE FROM MEDIA_TABLE WHERE id=99");
		bump_generation(GEN_MEDIA);
		queue_test_device_presync("/fixture/000099.flac");
		assert(current() == 0);
		bool ok = automatic ? playlist_advance_auto(path, sizeof(path)) : playlist_next(path, sizeof(path));
		assert(ok && path_id(path) == 1);
		printf("DEVICE_CURRENT_REMOVED_%s actual_presync_anchor=0 selected_id=1 preserved=PASS\n", automatic ? "AUTO" : "MANUAL");
	}
	for (int operation = 0; operation < 2; operation++) {
		for (int mutation = 0; mutation < 3; mutation++) {
			reset_library(696);
			load(99);
			assert(current() == 99);
			if (mutation == 0) {
				for (int i = 696; i < 727; i++)
					add_track(i, -1000 + i);
			} else if (mutation == 1) {
				runsql("DELETE FROM MEDIA_TABLE WHERE id < 20");
			} else {
				runsql("UPDATE MEDIA_TABLE SET ctime=-ctime");
			}
			bump_generation(GEN_MEDIA);
			bool ok = operation ? playlist_peek_auto(path, sizeof(path)) : playlist_advance_auto(path, sizeof(path));
			int expected = mutation == 2 ? 98 : 100;
			int got = ok ? path_id(path) : -1;
			mismatches += got != expected;
			if (operation)
				assert(current() == 99); // Peek must not advance playback.
			printf("%s_MUTATION_%d selected_id=%d expected=%d mismatch=%d\n", operation ? "PEEK" : "AUTO", mutation, got, expected, got != expected);
		}
	}
	reset_library(696);
	load(695);
	assert(current() == 695);
	for (int i = 696; i < 727; i++)
		add_track(i, 1000 + i);
	bump_generation(GEN_MEDIA);
	bool ended = playlist_at_end();
	mismatches += ended;
	printf("AT_END_APPEND cached_last=695 actual_end=%d expected=0 mismatch=%d\n", ended, ended);
	for (int operation = 0; operation < 2; operation++) {
		reset_library(696);
		load(695);
		assert(current() == 695);
		for (int i = 696; i < 727; i++)
			add_track(i, 1000 + i);
		bump_generation(GEN_MEDIA);
		bool ok = operation ? playlist_peek_auto(path, sizeof(path)) : playlist_advance_auto(path, sizeof(path));
		int got = ok ? path_id(path) : -1;
		mismatches += got != 696;
		if (operation)
			assert(current() == 695);
		printf("%s_AT_OLD_END selected_id=%d expected=696 mismatch=%d\n", operation ? "PEEK" : "AUTO", got, got != 696);
	}
	for (int operation = 0; operation < 2; operation++) {
		reset_library(696);
		load(694);
		assert(current() == 694);
		runsql("DELETE FROM MEDIA_TABLE WHERE id=695");
		bump_generation(GEN_MEDIA);
		bool ok = operation ? playlist_peek_auto(path, sizeof(path)) : playlist_advance_auto(path, sizeof(path));
		mismatches += ok;
		printf("%s_DELETE_TAIL returned=%d expected=0 mismatch=%d\n", operation ? "PEEK" : "AUTO", ok, ok);
	}
	reset_library(696);
	load(99);
	assert(current() == 99);
	playlist_set_mode(PLAYBACK_MODE_REPEAT_ONE);
	for (int i = 696; i < 727; i++)
		add_track(i, -1000 + i);
	bump_generation(GEN_MEDIA);
	bool peeked = playlist_peek_auto(path, sizeof(path));
	int repeat_id = peeked ? path_id(path) : -1;
	mismatches += repeat_id != 99;
	assert(current() == 99);
	assert(playlist_advance_auto(path, sizeof(path)) && path_id(path) == 99 && current() == 99);
	printf("REPEAT_ONE invalidated_source_peek_id=%d expected=99 mismatch=%d eof_id=99\n", repeat_id, repeat_id != 99);
	reset_library(696);
	load(695);
	assert(current() == 695);
	playlist_set_mode(PLAYBACK_MODE_REPEAT_ALL);
	assert(playlist_peek_auto(path, sizeof(path)) && path_id(path) == 0 && current() == 695);
	assert(playlist_advance_auto(path, sizeof(path)) && path_id(path) == 0);
	printf("REPEAT_ALL unchanged_last_wrap_to=0 peek_no_advance=PASS\n");
	reset_library(696);
	load(695);
	assert(current() == 695);
	playlist_set_mode(PLAYBACK_MODE_REPEAT_ALL);
	for (int i = 696; i < 727; i++)
		add_track(i, 1000 + i);
	bump_generation(GEN_MEDIA);
	bool advanced = playlist_advance_auto(path, sizeof(path));
	int appended_id = advanced ? path_id(path) : -1;
	mismatches += appended_id != 696;
	printf("REPEAT_ALL_APPEND selected_id=%d expected=696 mismatch=%d\n", appended_id, appended_id != 696);
	playlist_load_paths_ordered(explicit_paths, 3, 1);
	playlist_set_mode(PLAYBACK_MODE_REPEAT_ONE);
	assert(playlist_peek_auto(path, sizeof(path)) && path_id(path) == 42 && current() == 99);
	assert(playlist_advance_auto(path, sizeof(path)) && path_id(path) == 42);
	assert(playlist_at_end() && !playlist_advance_auto(path, sizeof(path)));
	printf("ORDERED_SEQUENCE ignores_repeat_one plays_once=PASS\n");
	reset_library(20000);
	library_index_t *large = library_index_open(LIBRARY_LIST_TRACKS, LIBRARY_FILTER_NONE, "", LIBRARY_ORDER_ADDED, false);
	assert(large && library_index_count(large) == 20000 && playlist_load_index(large, 9999));
	assert(current() == 9999 && playlist_is_library_backed());
	srand(123);
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
	assert(current() == 9999);
	snap = playlist_snapshot_take();
	assert(snap && playlist_snapshot_count(snap) == 20000);
	size_t saved_bytes = playlist_snapshot_bytes(snap);
	assert(saved_bytes < 20000 * (sizeof(size_t) + 4) + 2048);
	assert(playlist_snapshot_restore(snap, "/fixture/009999.flac") && current() == 9999);
	for (int i = 20000; i < 20031; i++)
		add_track(i, -100000 + i);
	bump_generation(GEN_MEDIA);
	assert(current() == 9999 && playlist_count() == 20031 && playlist_is_library_backed());
	next = advance_one(false);
	assert(next >= 0 && next < 20031);
	playlist_set_mode(PLAYBACK_MODE_NORMAL);
	assert(current() == next && playlist_count() == 20031);
	printf("LARGE_QUERY count_before=20000 count_after=20031 compact_snapshot_bytes=%zu shuffle_undo_and_mode=PASS\n", saved_bytes);
	playlist_clear();
	sqlite3_close(db);
	db = NULL;
	printf("RESULT mismatches=%u\n", mismatches);
	return mismatches ? 1 : 0;
}
