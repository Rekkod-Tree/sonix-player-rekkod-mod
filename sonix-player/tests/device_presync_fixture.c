// Execute the actual device-state pre-sync. These cases use custom/query
// queues, so reaching the folder fallback is an explicit fixture failure.
#include <assert.h>
#define playlist_load_folder fixture_folder_fallback
#include "src/system/playback/device_state.c"
#undef playlist_load_folder
void fixture_folder_fallback(const char *folder, const char *name) {
	(void)folder;
	(void)name;
	assert(!"unexpected non-custom folder fallback");
}
void queue_test_device_presync(const char *loaded) {
	snprintf(current_metadata_file, sizeof(current_metadata_file), "%s", loaded);
	playlist_resync_to_current();
}
