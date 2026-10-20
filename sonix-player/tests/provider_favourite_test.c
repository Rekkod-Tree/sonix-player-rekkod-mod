// cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I. tests/provider_favourite_test.c
// src/system/streaming/provider.c -Wl,--gc-sections -o /tmp/provider_favourite_test
#include "src/system/streaming/provider.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool local, remote;
static int local_writes, remote_writes;
bool library_fav_contains(const char *path) {
	(void)path;
	return local;
}
bool library_fav_toggle(const char *path, const char *title, const char *artist) {
	(void)path;
	(void)title;
	(void)artist;
	local_writes++;
	return local = !local;
}
static bool owns(const char *p) { return p && !strncmp(p, "cache/", 6); }
static bool get(const char *p) {
	(void)p;
	return remote;
}
static void set(const char *p, bool want, stream_provider_done_cb done, void *user) {
	(void)p;
	remote = want;
	remote_writes++;
	if (done)
		done(true, "", user);
}
int main(void) {
	stream_provider_t provider = {.id = "fixture", .owns_path = owns, .is_track = owns, .fav_get = get, .fav_toggle = set};
	stream_provider_register(&provider);
	assert(track_favourite_set("local/file", "title", "artist", true, NULL, NULL));
	assert(track_favourite_set("local/file", "title", "artist", true, NULL, NULL));
	assert(local_writes == 1 && track_favourite_get("local/file"));
	assert(track_favourite_set("cache/track", "title", "artist", true, NULL, NULL));
	assert(remote_writes == 1 && local_writes == 1 && track_favourite_get("cache/track"));
	provider.read_only = true;
	assert(!track_favourite_set("cache/track", "title", "artist", false, NULL, NULL));
	assert(!track_favourite_get("cache/track") && local_writes == 1 && remote_writes == 1);
	assert(!track_favourite_set("", "", "", true, NULL, NULL));
	puts("PASS local idempotence, provider dispatch, read-only cache never writes library");
}
