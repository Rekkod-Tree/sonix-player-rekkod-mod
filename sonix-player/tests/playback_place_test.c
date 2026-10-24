// Reuse the real SQLite/library fixture, not a second fake persistence layer.
#define main history_fixture_unused
#include "playhistory_confirmed_test.c"
#undef main
#include "src/gui/shell/playbackplace.h"
#include "src/system/streaming/provider.h"
static unsigned fake_queue = 1;
static library_index_spec_t fake_spec;
bool playlist_library_spec(library_index_spec_t *out) {
	*out = fake_spec;
	return fake_spec.valid;
}
const char *tr(const char *key) { return key; }
unsigned device_state_metadata_revision(void) { return fake_serial; }
unsigned playlist_source_revision(void) { return fake_queue; }
const stream_provider_t *stream_provider_for_path(const char *path) {
	(void)path;
	return NULL;
}

int main(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	assert(exec("CREATE TABLE PLAYBACK_PLACE(source TEXT PRIMARY KEY,route BLOB NOT NULL)"));
	atomic_store(&database_online, true);
	atomic_store(&database_epoch, 100);
	playbackplace_t place = {.kind = LIBRARY_LIST_TRACKS, .filter = LIBRARY_FILTER_ALBUM};
	snprintf(place.title, sizeof(place.title), "Album A");
	snprintf(place.a, sizeof(place.a), "album-id");
	playbackplace_t got;
	assert(!playbackplace_get("music", &got));
	playbackplace_arm("music", &place, "/a.flac");
	assert(!playbackplace_get("music", &got));
	playbackplace_heard("/old.flac"); // A request has not replaced the old sound yet.
	assert(!playbackplace_get("music", &got));
	playbackplace_heard("/a.flac");
	assert(playbackplace_get("music", &got) && !strcmp(got.title, "Album A"));
	assert(library_place_read_at(100, "music", &got, sizeof(got)));
	int changes = sqlite3_total_changes(db);
	for (int i = 0; i < 100; i++)
		playbackplace_heard("/a.flac");
	assert(sqlite3_total_changes(db) == changes); // No idle SQL writes.
	playbackplace_heard("/a-next.flac");
	assert(playbackplace_get("music", &got) && !strcmp(got.path, "/a-next.flac"));
	fake_queue++;
	playbackplace_heard("/another-queue.flac");
	assert(playbackplace_get("music", &got) && !strcmp(got.path, "/a-next.flac"));
	snprintf(place.title, sizeof(place.title), "Failed album");
	playbackplace_arm("music", &place, "/never-started.flac");
	fake_serial++;
	playbackplace_heard("/other.flac");
	playbackplace_heard("/never-started.flac");
	assert(playbackplace_get("music", &got) && !strcmp(got.title, "Album A"));
	assert(exec("BEGIN"));
	snprintf(place.title, sizeof(place.title), "Album B");
	playbackplace_arm("music", &place, "/b.flac");
	playbackplace_heard("/b.flac");
	assert(!sqlite3_get_autocommit(db));
	assert(library_place_read_at(100, "music", &got, sizeof(got)) && !strcmp(got.title, "Album A"));
	assert(exec("ROLLBACK"));
	playbackplace_heard("/b.flac");
	assert(library_place_read_at(100, "music", &got, sizeof(got)) && !strcmp(got.title, "Album B"));
	fake_queue++;
	fake_spec = (library_index_spec_t){.valid = true, .kind = LIBRARY_LIST_TRACKS, .filter = LIBRARY_FILTER_ALBUM};
	snprintf(fake_spec.value, sizeof(fake_spec.value), "Consecutive album");
	playbackplace_heard("/consecutive.flac");
	assert(playbackplace_get("music", &got) && !strcmp(got.title, "Consecutive album") && !strcmp(got.path, "/consecutive.flac"));
	fake_spec.valid = false;
	assert(exec("BEGIN"));
	playbackplace_arm("yandex", &place, "/old-card.flac");
	playbackplace_heard("/old-card.flac");
	assert(exec("ROLLBACK"));
	atomic_fetch_add(&database_epoch, 1);
	assert(exec("DELETE FROM PLAYBACK_PLACE"));
	playbackplace_heard("/old-card.flac");
	assert(!playbackplace_get("yandex", &got));
	assert(!library_place_store_at(100, "music", &place, sizeof(place)));
	assert(!library_place_store_at(101, "unknown-service", &place, sizeof(place)));
	assert(!library_place_store_at(101, "music", &place, 2049));
	place.version = 900;
	assert(library_place_store_at(101, "music", &place, sizeof(place)) == 1);
	assert(!playbackplace_get("music", &got));
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	puts("PASS confirmed route, failed request, independent browsing, foreign transaction, stale card, bounded format and zero idle writes");
	return 0;
}
