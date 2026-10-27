/*
 * Focused queue-persistence epoch fixture. Includes the real device_state.c
 * path and mocks only the playlist/library boundary; no card or SQLite I/O.
 * Build with function/data sections and --gc-sections.
 */
#include "src/system/playback/device_state.c"
#include <assert.h>

static unsigned fake_epoch = 10;
static unsigned fake_revision = 7;
static int fake_current = 1;
static bool fake_library_backed;
static bool fake_library_spec;
static const char *live_paths[] = {"/music/one.flac", "/music/two.flac"};
static int live_count = 2;
static const char *db_paths[] = {"/music/one.flac", "/music/two.flac", "/music/three.flac"};
static int db_count = 2;
static bool db_custom;
static unsigned queue_save_calls;
static unsigned stale_checkpoint_calls;
static unsigned stale_checkpoint_accepted;
static unsigned entry_checkpoint_calls;
static unsigned entry_checkpoint_accepted;
static unsigned entry_checkpoint_epoch;
static int entry_checkpoint_index;
static int entry_checkpoint_count;
static bool entry_checkpoint_custom;
static char entry_checkpoint_path[512];

unsigned library_database_epoch(void) { return fake_epoch; }

void metadata_read(const char *path, song_metadata_t *out) {
	assert(path && out);
	memset(out, 0, sizeof(*out));
}

bool stream_provider_fill_metadata(const char *path, song_metadata_t *out) {
	(void)path;
	(void)out;
	return false;
}

bool stream_provider_owns(const char *path) {
	(void)path;
	return false;
}

int playlist_count(void) { return live_count; }
int playlist_current_index(void) { return fake_current; }
unsigned playlist_revision(void) { return fake_revision; }
bool playlist_library_spec(library_index_spec_t *out) {
	if (out)
		memset(out, 0, sizeof(*out));
	if (out)
		out->valid = fake_library_spec;
	return fake_library_spec;
}
bool playlist_is_library_backed(void) { return fake_library_backed; }
bool playlist_path_at(int index, char *out, size_t size) {
	if (index < 0 || index >= live_count || !out || !size)
		return false;
	snprintf(out, size, "%s", live_paths[index]);
	return true;
}
bool playlist_is_custom(void) { return false; }
size_t playlist_current_entry(void) { return (size_t)fake_current; }
int playlist_appended_count(void) { return 0; }
int playlist_appended_slots(int *slots, int count) {
	(void)slots;
	(void)count;
	return 0;
}
bool playlist_appended_at(int index, char *out, size_t size) {
	(void)index;
	(void)out;
	(void)size;
	return false;
}

void library_checkpoint_queue_at(unsigned epoch, int index, bool query) {
	(void)index;
	(void)query;
	stale_checkpoint_calls++;
	if (epoch == fake_epoch)
		stale_checkpoint_accepted++;
}

void library_checkpoint_queue_entry_at(unsigned epoch, int index, int count, bool custom, const char *path) {
	entry_checkpoint_calls++;
	entry_checkpoint_epoch = epoch;
	entry_checkpoint_index = index;
	entry_checkpoint_count = count;
	entry_checkpoint_custom = custom;
	snprintf(entry_checkpoint_path, sizeof(entry_checkpoint_path), "%s", path ? path : "");
	if (epoch == fake_epoch && count == db_count && custom == db_custom && index >= 0 && index < db_count && path && strcmp(db_paths[index], path) == 0)
		entry_checkpoint_accepted++;
}

void library_queue_save(const char *const *paths, int count, int current_index, bool custom) {
	(void)paths;
	(void)count;
	(void)current_index;
	(void)custom;
	queue_save_calls++;
}

void library_queue_save_query(const library_index_spec_t *spec, int position, const char *const *extra, const int *extra_slots, int extra_count) {
	(void)spec;
	(void)position;
	(void)extra;
	(void)extra_slots;
	(void)extra_count;
	queue_save_calls++;
}

static void setup(void) {
	fake_epoch = 11;
	fake_revision = 7;
	fake_current = 1;
	fake_library_backed = fake_library_spec = false;
	live_paths[0] = "/music/one.flac";
	live_paths[1] = "/music/two.flac";
	live_count = 2;
	db_paths[0] = "/music/one.flac";
	db_paths[1] = "/music/two.flac";
	db_count = 2;
	db_custom = false;
	queue_save_calls = stale_checkpoint_calls = stale_checkpoint_accepted = 0;
	entry_checkpoint_calls = entry_checkpoint_accepted = 0;
	entry_checkpoint_epoch = 0;
	entry_checkpoint_index = -1;
	entry_checkpoint_count = 0;
	entry_checkpoint_custom = false;
	entry_checkpoint_path[0] = '\0';
	queue_ever_saved = true;
	saved_queue_revision = fake_revision;
	saved_queue_epoch = 10; // Previous library open; checkpoint API must reject it.
	strcpy(current_metadata_file, "/music/two.flac");
	read_current_metadata(); // Explicit track reload captured in the new epoch.
}

static void same_queue_reopened_same_card(void) {
	setup();
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 1);
	assert(entry_checkpoint_epoch == fake_epoch && entry_checkpoint_index == fake_current);
	assert(entry_checkpoint_count == live_count && !entry_checkpoint_custom);
	assert(strcmp(entry_checkpoint_path, current_metadata_file) == 0);
	assert(saved_queue_epoch == 10); // The cached old queue never adopts the new epoch.
	assert(queue_save_calls == 0);
}

static void different_neighbors_same_current_slot(void) {
	setup();
	db_paths[0] = "/other-card/neighbor.flac";
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 1);
	assert(strcmp(db_paths[0], live_paths[0]) != 0); // Neighbor rows are not compared or written.
	assert(saved_queue_epoch == 10 && queue_save_calls == 0);
}

static void mismatched_path_is_rejected(void) {
	setup();
	db_paths[1] = "/other-card/two.flac";
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 0);
	assert(saved_queue_epoch == 10 && queue_save_calls == 0);
}

static void mismatched_index_is_rejected(void) {
	setup();
	db_paths[0] = "/music/two.flac";
	db_paths[1] = "/music/one.flac";
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 0);
	assert(entry_checkpoint_index == 1 && strcmp(db_paths[0], entry_checkpoint_path) == 0);
	assert(saved_queue_epoch == 10 && queue_save_calls == 0);
}

static void mismatched_count_is_rejected(void) {
	setup();
	db_paths[2] = "/other-card/three.flac";
	db_count = 3;
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 0);
	assert(saved_queue_epoch == 10 && queue_save_calls == 0);
}

static void mismatched_custom_is_rejected(void) {
	setup();
	db_custom = true;
	queue_persist();
	assert(entry_checkpoint_calls == 1 && entry_checkpoint_accepted == 0);
	assert(saved_queue_epoch == 10 && queue_save_calls == 0);
}

static void mismatched_live_track_is_not_submitted(void) {
	setup();
	strcpy(current_metadata_file, "/not-the-current-queue-entry.flac");
	queue_persist();
	assert(entry_checkpoint_calls == 0 && stale_checkpoint_calls == 1);
	assert(stale_checkpoint_accepted == 0);
	assert(saved_queue_epoch == 10);
	assert(queue_save_calls == 0);
}

static void no_explicit_reload_means_no_rebind(void) {
	setup();
	current_metadata_epoch = 10; // The newly opened database was not explicitly adopted.
	queue_persist();
	assert(entry_checkpoint_calls == 0 && stale_checkpoint_calls == 1);
	assert(stale_checkpoint_accepted == 0);
	assert(saved_queue_epoch == 10);
	assert(queue_save_calls == 0);
}

static void library_queue_keeps_its_existing_epoch_path(void) {
	setup();
	fake_library_backed = true;
	fake_library_spec = true;
	queue_persist();
	assert(entry_checkpoint_calls == 0 && stale_checkpoint_calls == 1);
	assert(stale_checkpoint_accepted == 0 && saved_queue_epoch == 10);
	assert(queue_save_calls == 0);
}

static void stale_library_spec_is_not_treated_as_path_queue(void) {
	setup();
	fake_library_backed = true;
	fake_library_spec = false;
	queue_persist();
	assert(entry_checkpoint_calls == 0 && stale_checkpoint_calls == 1);
	assert(stale_checkpoint_accepted == 0 && saved_queue_epoch == 10);
	assert(queue_save_calls == 0);
}

int main(int argc, char **argv) {
	assert(argc == 2);
	if (strcmp(argv[1], "same-card") == 0) {
		same_queue_reopened_same_card();
		puts("PASS validated entry checkpoint without queue epoch adoption");
	} else if (strcmp(argv[1], "different-neighbors") == 0) {
		different_neighbors_same_current_slot();
		puts("PASS different neighbors with same current slot update position only");
	} else if (strcmp(argv[1], "mismatched-path") == 0) {
		mismatched_path_is_rejected();
		puts("PASS mismatched path rejected");
	} else if (strcmp(argv[1], "mismatched-index") == 0) {
		mismatched_index_is_rejected();
		puts("PASS mismatched index rejected");
	} else if (strcmp(argv[1], "mismatched-count") == 0) {
		mismatched_count_is_rejected();
		puts("PASS mismatched queue count rejected");
	} else if (strcmp(argv[1], "mismatched-custom") == 0) {
		mismatched_custom_is_rejected();
		puts("PASS mismatched custom flag rejected");
	} else if (strcmp(argv[1], "mismatched-live-track") == 0) {
		mismatched_live_track_is_not_submitted();
		puts("PASS live-track mismatch does not submit entry checkpoint");
	} else if (strcmp(argv[1], "stale-metadata") == 0) {
		no_explicit_reload_means_no_rebind();
		puts("PASS stale metadata does not authorize entry checkpoint");
	} else if (strcmp(argv[1], "library-query") == 0) {
		library_queue_keeps_its_existing_epoch_path();
		puts("PASS library query queue retains existing epoch checkpoint");
	} else if (strcmp(argv[1], "stale-library-spec") == 0) {
		stale_library_spec_is_not_treated_as_path_queue();
		puts("PASS invalid library spec cannot rebind as path queue");
	} else {
		assert(!"usage: queue_epoch_rebind_test case");
	}
	return 0;
}
