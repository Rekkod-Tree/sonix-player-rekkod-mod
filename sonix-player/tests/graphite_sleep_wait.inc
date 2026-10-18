// A late network answer must not restart playback after the sleep deadline.
#include <dirent.h>
#include "src/system/streaming/yandexlist.h"
#include "src/system/streaming/yandexplay.h"
static void sleep_wait_states(void) {
	static int step;
	static bool resumed;
	device_state_t state;
	int current = step++;
	if (current == 0) {
		gui_init_rest();
		assert(yandex_logged_in());
		player_play_file(getenv("SONIX_UI_TEST_TRACK"));
		return;
	}
	if (current == 1) {
		sleeptimer_start(1);
		player_refresh_now_playing();
		yandexpage_wave_open();
		return;
	}
	if (current == 2) {
		lv_obj_t *label = find_label(lv_layer_top(), tr("medialist_play"));
		assert(label);
		lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
		return;
	}
	if (current == 55 || current == 75) {
		device_state_get(&state);
		printf("SLEEP_WAIT checkpoint %d enabled=%d status=%d file=%s\n", current,
			sleeptimer_enabled(SLEEPTIMER_MUSIC), state.status, state.current_file);
		fflush(stdout);
		capture(current == 55 ? "sleep-wait-before" : "sleep-wait-after");
	}
	if (current > 65 && current <= 100) {
		device_state_get(&state);
		if (!sleeptimer_enabled(SLEEPTIMER_MUSIC) && state.status == AUDIO_STATUS_PLAYING) resumed = true;
	}
	if (current < 100) return;
	if (current > 100) {
		device_state_get(&state);
		if (state.status == AUDIO_STATUS_PLAYING && strstr(state.current_file, "/101.mp3")) {
			puts("UI_TEST_PASS sleep wait: late answer stays paused; a new explicit Play still starts");
			fflush(stdout); exit(0);
		}
		assert(current < 230);
		return;
	}
	char cache[640]; snprintf(cache, sizeof(cache), "%s/.local/yandex-cache", getenv("SONIX_SD_ROOT"));
	DIR *folder = opendir(cache); assert(folder);
	struct dirent *item; bool complete = false;
	while ((item = readdir(folder))) {
		char file[1024]; struct stat info;
		snprintf(file, sizeof(file), "%s/%s", cache, item->d_name);
		if (strstr(item->d_name, ".mp3") && !stat(file, &info) && info.st_size == atol(getenv("SONIX_UI_TEST_WAIT_BYTES"))) complete = true;
	}
	closedir(folder);
	assert(complete); // The late media answer actually arrived; otherwise this test proves nothing.

	device_state_get(&state);
	printf("SLEEP_WAIT final enabled=%d status=%d file=%s\n", sleeptimer_enabled(SLEEPTIMER_MUSIC), state.status, state.current_file);
	fflush(stdout);
	assert(!sleeptimer_enabled(SLEEPTIMER_MUSIC));
	assert(!resumed && state.status != AUDIO_STATUS_PLAYING);
	yandexlist_t *next = yandexlist_new(); assert(next);
	yandex_track_t track = {.available = true};
	snprintf(track.id, sizeof(track.id), "101");
	snprintf(track.album_id, sizeof(track.album_id), "11");
	snprintf(track.title, sizeof(track.title), "Manual Play after sleep");
	assert(yandexlist_add_track(next, &track));
	assert(yandexplay_list(next, 0, false));
	yandexlist_free(next);
}
