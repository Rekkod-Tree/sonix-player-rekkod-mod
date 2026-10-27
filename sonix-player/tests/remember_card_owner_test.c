// Actual playback owner's capture and eligibility rules; no decoder/card I/O.
// Build with function/data sections and --gc-sections; LVGL include path needed.
#include "src/system/playback/device_state.c"
#include <assert.h>

static unsigned fake_epoch = 10;
static bool replace_during_read;
unsigned library_database_epoch(void) { return fake_epoch; }
bool stream_provider_owns(const char *path) { return !strncmp(path, "service:", 8); }
bool stream_provider_fill_metadata(const char *path, song_metadata_t *out) {
	(void)path;
	(void)out;
	return false;
}
void metadata_read(const char *path, song_metadata_t *out) {
	assert(path && out);
	memset(out, 0, sizeof(*out));
	if (replace_during_read)
		fake_epoch++;
}

int main(void) {
	device_state_t state = {0};
	snprintf(current_metadata_file, sizeof(current_metadata_file), "/card/a.flac");
	snprintf(state.current_file, sizeof(state.current_file), "%s", current_metadata_file);
	read_current_metadata();
	assert(remember_worth_saving(&state));
	fake_epoch++;
	assert(!remember_worth_saving(&state)); // A poll cannot adopt a new card.
	read_current_metadata();				// An explicit new load establishes its own identity.
	assert(remember_worth_saving(&state));
	replace_during_read = true;
	read_current_metadata();
	assert(!remember_worth_saving(&state)); // Capture before any blocking read.
	replace_during_read = false;
	read_current_metadata();
	snprintf(state.current_file, sizeof(state.current_file), "/card/old-decoder.flac");
	assert(!remember_worth_saving(&state)); // Requested and actual track differ.
	snprintf(state.current_file, sizeof(state.current_file), "%s", current_metadata_file);
	state.live = true;
	assert(!remember_worth_saving(&state));
	state.live = false;
	fake_epoch = 0;
	assert(!remember_worth_saving(&state));
	puts("PASS remembered position uses playback-origin card and matching actual track");
	return 0;
}
