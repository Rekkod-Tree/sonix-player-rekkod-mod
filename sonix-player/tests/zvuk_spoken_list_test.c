// Real compact list/clone/LRU; normal endpoint replies, no network or UI.
#include <stdbool.h>
#include <stdlib.h>
static void *fixture_malloc(size_t);
#define malloc fixture_malloc
#include "src/system/streaming/zvuklist.c"
#undef malloc
#include <assert.h>

static bool fail_malloc, omit_last;
static int music_calls, episode_calls;
static void *fixture_malloc(size_t size) {
	if (fail_malloc) {
		fail_malloc = false;
		return NULL;
	}
	return malloc(size);
}
static int fetch(const char *const *ids, int count, zvuk_track_t *out, int max, bool episodes) {
	assert(count <= 50 && max >= count);
	if (episodes)
		episode_calls++;
	else
		music_calls++;
	int n = count - (omit_last ? 1 : 0);
	for (int i = 0; i < n; i++) {
		memset(&out[i], 0, sizeof(out[i]));
		snprintf(out[i].id, sizeof(out[i].id), "%s", ids[i]);
		snprintf(out[i].title, sizeof(out[i].title), "Normal %s", ids[i]);
		out[i].content = episodes ? ZVUK_CONTENT_EPISODE : ZVUK_CONTENT_TRACK;
		out[i].available = true;
		if (episodes)
			strcpy(out[i].published_at, "2026-10-17T00:00:00Z");
	}
	return n;
}
int zvuk_tracks(const char *const *ids, int count, zvuk_track_t *out, int max) { return fetch(ids, count, out, max, false); }
int zvuk_episodes(const char *const *ids, int count, zvuk_track_t *out, int max) { return fetch(ids, count, out, max, true); }
static void add_ids(zvuklist_t *list, int count) {
	for (int i = 0; i < count; i++) {
		char id[24];
		snprintf(id, sizeof(id), "%d", 1000 + i);
		assert(zvuklist_add_id(list, id));
	}
}
int main(void) {
	zvuklist_t *podcast = zvuklist_new_episodes();
	add_ids(podcast, 338);
	assert(zvuklist_memory(podcast) < 16000 && episode_calls == 0);
	assert(zvuklist_fetch_page(podcast, 0) == 50 && episode_calls == 1 && music_calls == 0);
	assert(zvuklist_fetch_page(podcast, 6) == 38 && episode_calls == 2);
	zvuk_track_t track;
	assert(zvuklist_track(podcast, 337, &track) && !strcmp(track.id, "1337") && track.published_at[0]);
	zvuklist_free(podcast);
	podcast = zvuklist_new_episodes();
	add_ids(podcast, 800);
	for (int page = 0; page < 16; page++)
		assert(zvuklist_fetch_page(podcast, page) == 50);
	assert(!zvuklist_page_ready(podcast, 0));
	zvuklist_t *clone = zvuklist_clone(podcast);
	int before = episode_calls;
	assert(clone && zvuklist_fetch_page(clone, 0) == 50 && episode_calls == before + 1);
	assert(zvuklist_track(clone, 0, &track) && track.content == ZVUK_CONTENT_EPISODE);
	zvuklist_free(clone);
	zvuklist_free(podcast);
	// Mixed music IDs plus an explicitly queued episode keep its known details.
	zvuklist_t *mixed = zvuklist_new();
	assert(zvuklist_add_id(mixed, "1"));
	memset(&track, 0, sizeof(track));
	strcpy(track.id, "2");
	strcpy(track.title, "Queued episode");
	strcpy(track.published_at, "2026-10-17");
	track.content = ZVUK_CONTENT_EPISODE;
	assert(zvuklist_add_track(mixed, &track) && !zvuklist_page_ready(mixed, 0));
	omit_last = true;
	assert(zvuklist_fetch_page(mixed, 0) == 2 && zvuklist_page_ready(mixed, 0));
	clone = zvuklist_clone(mixed);
	assert(clone && zvuklist_track(clone, 1, &track) && track.content == ZVUK_CONTENT_EPISODE && track.published_at[0]);
	assert(clone->pinned);
	zvuklist_free(clone);
	fail_malloc = true;
	assert(zvuklist_clone(mixed) == NULL); // never silently lose the episode on OOM
	zvuklist_free(mixed);
	// A removed API item becomes an unavailable row, not an endless page request.
	mixed = zvuklist_new();
	add_ids(mixed, 2);
	assert(zvuklist_fetch_page(mixed, 0) == 2);
	before = music_calls;
	assert(zvuklist_page_ready(mixed, 0) && zvuklist_fetch_page(mixed, 0) == 2 && music_calls == before);
	assert(zvuklist_track(mixed, 1, &track) && !track.available);
	zvuklist_free(mixed);
	puts("PASS: 338 compact IDs, episode pages/LRU/clone, mixed pinned episode, clone OOM, partial API settles");
	return 0;
}
