// Production queue/index regression, including indexed Play Next ownership.
// Compile playlist.c separately with -Dcalloc=queue_test_calloc for injection.
#define main queue_refresh_checks
#include "queue_refresh_test.c"
#undef main

static bool fail_next_calloc;
void *queue_test_calloc(size_t count, size_t size) {
	if (fail_next_calloc) {
		fail_next_calloc = false;
		return NULL;
	}
	return calloc(count, size);
}

static void next_is(const char *expected) {
	char path[512];
	assert(playlist_path_at(playlist_current_index() + 1, path, sizeof(path)));
	assert(!strcmp(path, expected));
}

int main(void) {
	setbuf(stdout, NULL);
	assert(queue_refresh_checks() == 0);
	open_test_db();
	reset_library(32);
	library_index_t *ix = library_index_open(LIBRARY_LIST_TRACKS, LIBRARY_FILTER_NONE, "", LIBRARY_ORDER_ADDED, false);
	assert(ix && playlist_load_index(ix, 4));
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
	char dealt[32][512], got[512], playing[512], promised[512];
	assert(playlist_current_path(playing, sizeof(playing)));
	for (int i = 0; i < 32; i++)
		assert(playlist_path_at(i, dealt[i], sizeof(dealt[i])));
	unsigned revision = playlist_revision();
	for (int i = 0; i < 128; i++)
		assert(playlist_path_at((i * 17) % 32, got, sizeof(got)) && !strcmp(got, dealt[(i * 17) % 32]));
	assert(playlist_revision() == revision);
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE_REPEAT);
	for (int i = 0; i < 32; i++)
		assert(playlist_path_at(i, got, sizeof(got)) && !strcmp(got, dealt[i]));
	for (int i = 5; i < 32; i++)
		assert(playlist_next(got, sizeof(got)) && !strcmp(got, dealt[i]));
	assert(playlist_set_current(4));
	assert(playlist_path_at(9, promised, sizeof(promised)));
	// The new bitmap is reserved before any order or position mutation.
	fail_next_calloc = true;
	assert(!playlist_move_next(9) && !fail_next_calloc);
	assert(playlist_revision() == revision);
	assert(playlist_current_path(got, sizeof(got)) && !strcmp(got, playing));
	for (int i = 0; i < 32; i++)
		assert(playlist_path_at(i, got, sizeof(got)) && !strcmp(got, dealt[i]));
	assert(playlist_move_next(9));
	next_is(promised);
	playlist_snapshot_t *snap = playlist_snapshot_take();
	assert(snap && playlist_snapshot_count(snap) == 32);
	assert(playlist_shuffle_upcoming());
	assert(playlist_snapshot_restore(snap, playing));
	next_is(promised);
	playback_mode_t modes[] = {PLAYBACK_MODE_NORMAL, PLAYBACK_MODE_SHUFFLE, PLAYBACK_MODE_REPEAT_ALL};
	for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
		playlist_set_mode(modes[i]);
		next_is(promised);
		assert(playlist_current_path(got, sizeof(got)) && !strcmp(got, playing));
	}
	// A rescan inserts before the current row and changes every storage offset.
	add_track(100, -2000);
	bump_generation(GEN_MEDIA);
	assert(playlist_current_path(got, sizeof(got)) && !strcmp(got, playing));
	assert(playlist_count() == 33);
	next_is(promised);
	// Removed promises must disappear, never transfer to the replacement row.
	char sql[160];
	snprintf(sql, sizeof(sql), "DELETE FROM MEDIA_TABLE WHERE id=%d", path_id(promised));
	runsql(sql);
	bump_generation(GEN_MEDIA);
	assert(playlist_current_path(got, sizeof(got)) && !strcmp(got, playing));
	assert(playlist_count() == 32);
	for (int i = 0; i < playlist_count(); i++)
		assert(playlist_path_at(i, got, sizeof(got)) && strcmp(got, promised));
	assert(playlist_set_current(31));
	assert(playlist_path_at(0, promised, sizeof(promised)));
	assert(playlist_peek_auto(got, sizeof(got)) && !strcmp(got, promised));
	assert(playlist_advance_auto(got, sizeof(got)) && !strcmp(got, promised));
	// Already-played library rows can also be explicitly moved to next.
	assert(playlist_set_current(10));
	assert(playlist_path_at(2, promised, sizeof(promised)));
	assert(playlist_current_path(playing, sizeof(playing)));
	assert(playlist_move_next(2));
	playlist_set_mode(PLAYBACK_MODE_SHUFFLE);
	next_is(promised);
	assert(playlist_current_path(got, sizeof(got)) && !strcmp(got, playing));
	// Materialization during removal must retain the indexed promise.
	int remove = playlist_count() - 1;
	assert(playlist_remove(&remove, 1));
	playlist_set_mode(PLAYBACK_MODE_NORMAL);
	next_is(promised);
	playlist_clear();
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	puts("PASS queue order, 27 actual nexts, mode stability, indexed Play Next, allocation failure, Undo, rescan, removal and repeat wrap");
	return 0;
}
