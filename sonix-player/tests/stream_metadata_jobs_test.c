// Link with the host production objects using stream_metadata_jobs_test.py.
// Includes the real worker callbacks; never starts the worker or audio/UI.
#include <assert.h>
#ifdef TEST_ZVUK
#define zvukcache_track_id fixture_track_id
#define zvukcache_owns fixture_owns
#define zvuk_logged_in fixture_logged_in
#include "src/system/streaming/zvukplay.c"
#define list_new zvuklist_new
#define list_add zvuklist_add_track
#define list_free zvuklist_free
#define track_t zvuk_track_t
#define peek_metadata zvukplay_peek_metadata
#define prepare_track zvukplay_prepare_track
#define fixture_tracks __wrap_zvuk_tracks
#else
#define yandexcache_track_id fixture_track_id
#define yandexcache_owns fixture_owns
#define yandex_logged_in fixture_logged_in
#include "src/system/streaming/yandexplay.c"
#define list_new yandexlist_new
#define list_add yandexlist_add_track
#define list_free yandexlist_free
#define track_t yandex_track_t
#define peek_metadata yandexplay_peek_metadata
#define prepare_track yandexplay_prepare_track
#define fixture_tracks __wrap_yandex_tracks
#endif

static char cached_path[] = "/tmp/sonix-metadata-cache-XXXXXX";
bool fixture_owns(const char *path) { return path && strcmp(path, cached_path) == 0; }
bool fixture_logged_in(void) { return true; }
int fixture_tracks(const char *const *ids, int count, track_t *out, int max) {
	assert(count == 1 && max >= 1 && strcmp(ids[0], "123") == 0);
	memset(out, 0, sizeof(*out));
	snprintf(out->id, sizeof(out->id), "123");
	snprintf(out->title, sizeof(out->title), "Normal cached track");
	return 1;
}
void fixture_track_id(const char *path, char *out, size_t size) {
	snprintf(out, size, "%s", path && (strcmp(path, "/fixture/123.m4a") == 0 || fixture_owns(path)) ? "123" : "");
}

int main(void) {
	queue = list_new();
	assert(queue);
	track_t track = {0};
	snprintf(track.id, sizeof(track.id), "123");
	snprintf(track.title, sizeof(track.title), "Normal cached track");
	snprintf(track.artist, sizeof(track.artist), "Artist");
	assert(list_add(queue, &track));
	song_metadata_t meta = {0};
	assert(peek_metadata("/fixture/123.m4a", &meta));
	assert(strcmp(meta.title, track.title) == 0);
	assert(!peek_metadata("/fixture/unknown.m4a", &meta));

	// No worker is created: exercise the actual bounded job admission only.
	worker_started = true;
	queue_generation = 7;
	submit(JOB_FETCH, "999", true, NULL);
	submit(JOB_METADATA, "123", true, "/fixture/123.m4a");
	submit(JOB_METADATA, "456", false, "/fixture/456.m4a");
	assert(strcmp(metadata_pending.id, "123") == 0);
	assert(strcmp(fetch_pending.id, "999") == 0);
	metadata_pending.kind = JOB_NONE; // Worker took the current request.
	submit(JOB_METADATA, "456", false, "/fixture/456.m4a");
	assert(strcmp(metadata_pending.id, "456") == 0);
	submit(JOB_METADATA, "789", true, "/fixture/789.m4a");
	assert(strcmp(metadata_pending.id, "789") == 0);

	unsigned revision = device_state_metadata_revision();
	unsigned persisted = playlist_revision();
	job_result_t *r = calloc(1, sizeof(*r));
	r->kind = JOB_FETCH;
	r->generation = 6;
	r->ok = true;
	fetch_done_async(r); // Stale success must never touch queue or playback.
	assert(device_state_metadata_revision() == revision);
	assert(playlist_revision() == persisted);
	r = calloc(1, sizeof(*r));
	r->kind = JOB_METADATA;
	r->generation = 7;
	r->waiting = true; // Current priority is NOT permission to replay audio.
	r->ok = true;
	snprintf(r->path, sizeof(r->path), "/fixture/123.m4a");
	fetch_done_async(r);
	assert(device_state_metadata_revision() == revision + 1);
	assert(playlist_revision() == persisted);
	list_free(queue);
	queue = NULL;

	// Real prepare fast-path: an existing cache file without tags and without
	// a loaded API page schedules metadata only, while playback can continue.
	int fd = mkstemp(cached_path);
	assert(fd >= 0);
	close(fd);
	queue_generation++;
	metadata_pending.kind = JOB_NONE;
	assert(prepare_track(cached_path));
	assert(metadata_pending.kind == JOB_METADATA && metadata_pending.waiting);
	assert(strcmp(metadata_pending.path, cached_path) == 0);
	assert(strcmp(fetch_pending.id, "999") == 0);
	track_details(queue, "123", &track); // Real page parser, normal API stub.
	assert(strcmp(track.title, "Normal cached track") == 0);
	assert(peek_metadata(cached_path, &meta));
	assert(strcmp(meta.title, track.title) == 0);
	char tags[512];
	snprintf(tags, sizeof(tags), "%s.tags", cached_path);
	assert(access(tags, F_OK) != 0); // No tag/cover writes in metadata repair.
	assert(unlink(cached_path) == 0);
	list_free(queue);
	queue = NULL;
	puts("PASS: memory peek, bounded current/next priority, stale result, metadata-only notification");
	return 0;
}
