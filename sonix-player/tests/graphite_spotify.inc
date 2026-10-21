// Disposable protocol fixture; never uses an account or a production card.
#include "src/gui/streaming/spotifypage.h"
#include "src/system/streaming/spotify.h"
#include "src/system/core/config.h"
#include "src/system/streaming/provider.h"
#include "src/system/audio/replaygain.h"
#include <math.h>
static void spotify_test_click(const char *text) {
	lv_obj_t *label = find_label(lv_screen_active(), text);
	assert(label && visible(label));
	lv_area_t box; lv_obj_get_coords(label, &box);
	input((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2, true);
	input(point.x, point.y, false);
}
static void spotify_states(void) {
	static int step, wait, saved_scroll, count, index;
	static lv_obj_t *scroller, *catalog;
	device_state_t state;
	switch (step++) {
	case 0:
		gui_init_rest();
		device_state_restore_file("/mnt/sd_0/Echoes.flac", 0);
		player_refresh_now_playing();
		config_set_int("audio", "replaygain", REPLAYGAIN_TRACK);
		{
			char path[1024]; const char *config = config_path_copy((char[512]){0}, 512); const char *slash = strrchr(config, '/'); assert(slash);
			snprintf(path, sizeof(path), "%.*s/spotify", (int)(slash - config), config); assert(mkdir(path,0700)==0);
			strcat(path,"/credentials.json"); FILE *file=fopen(path,"w"); assert(file); assert(!fchmod(fileno(file),0600));
			fputs("{\"username\":\"fixture\",\"auth_type\":1,\"auth_data\":\"AA==\"}",file); fclose(file);
		}
		pointer = lv_indev_create(); lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(pointer, read_pointer);
		theme_set_dark(true); spotifypage_open(); return;
	case 1:
		capture("spotify-home-dark"); spotify_test_click(tr("spotify_liked")); step = 101; return;
	case 101:
		spotifypage_open(); step = 102; return;
	case 102:
		spotify_test_click(tr("spotify_playlists")); step = 2; return;
	case 2: {
		lv_obj_t *track = find_label(lv_screen_active(), "Трек 01");
		if (!track && find_label(lv_screen_active(), "Плейлист 01")) { spotify_test_click("Плейлист 01"); step--; return; }
		if (!track) { if (++wait == 8) capture("spotify-catalog-wait"); assert(wait < 12); step--; return; }
		capture("spotify-catalog-dark");
		catalog = lv_screen_active(); scroller = track;
		while (lv_obj_get_parent(scroller) != catalog) scroller = lv_obj_get_parent(scroller);
		lv_obj_scroll_to_y(scroller, 440, LV_ANIM_OFF); return;
	}
	case 3:
		saved_scroll = lv_obj_get_scroll_y(scroller); assert(saved_scroll > 300);
		spotify_stop(); spotifypage_open(); return;
	case 4:
		spotify_test_click(tr("spotify_continue")); return;
	case 5:
		assert(lv_screen_active() == catalog && abs(lv_obj_get_scroll_y(scroller) - saved_scroll) <= 1);
		capture("spotify-restored-scroll");
		spotify_test_click("Трек 08"); return;
	case 6:
		device_state_get(&state);
		if (!state.metadata.title[0] || !strstr(state.metadata.title, "Трек")) { assert(++wait < 20); step--; return; }
		assert(player_sheet_is_open());
		assert(state.metadata.has_track_gain && fabs(state.metadata.track_gain_db + 3.5) < 0.001);
		assert(fabs(replaygain_current_db() + 3.5) < 0.001);
		assert(!track_favourite_set(state.current_file, "fixture", "fixture", true, NULL, NULL));
		assert(!find_image(lv_screen_active(), &icon_star) || !visible(find_image(lv_screen_active(), &icon_star)));
		count = playlist_count(); index = playlist_current_index(); assert(count == 20 && index == 7);
		capture("spotify-player"); player_sheet_close(false); return;
	case 7:
		switch_screen(yandex_screen); return;
	case 8:
		assert(playlist_count() == count && playlist_current_index() == index);
		capture("spotify-queue-survives-service-browse"); spotifypage_open(); return;
	case 9:
		spotify_test_click(tr("spotify_continue")); return;
	case 10:
		capture("spotify-return-check");
		printf("SPOTIFY_RETURN screen=%d scroll=%d saved=%d\n", lv_screen_active() == catalog, (int)lv_obj_get_scroll_y(scroller), saved_scroll); fflush(stdout);
		assert(lv_screen_active() == catalog && abs(lv_obj_get_scroll_y(scroller) - saved_scroll) <= 1);
		assert(playlist_count() == count && playlist_current_index() == index);
		theme_set_dark(false); capture("spotify-restored-light");
		if (getenv("SONIX_UI_TEST_SPOTIFY_SLEEP")) { sleeptimer_start(1); wait = 0; return; }
		device_state_stop(); spotify_stop();
		printf("UI_TEST_PASS Spotify catalogue/scroll/queue/service-browse, both themes\n"); fflush(stdout); exit(0);
	case 11:
		step--;
		wait++;
		if (wait == 55) { assert(device_state_play_queue_index(8)); capture("spotify-sleep-pending"); }
		if (wait < 67) return;
		device_state_get(&state);
		assert(!sleeptimer_enabled(SLEEPTIMER_MUSIC) && state.status != AUDIO_STATUS_PLAYING);
		assert(!stream_provider_any_downloading());
		capture("spotify-sleep-cancelled");
		assert(device_state_play_queue_index(0));
		step = 12; wait = 0; return;
	case 12:
		device_state_get(&state);
		if (state.status != AUDIO_STATUS_PLAYING) { assert(++wait < 12); step--; return; }
		assert(playlist_count() == count && playlist_current_index() == 0);
		capture("spotify-manual-play-after-sleep");
		device_state_stop(); spotify_stop();
		puts("UI_TEST_PASS Spotify scroll/queue/service-browse/sleep-pending-cancel/manual-resume"); fflush(stdout); exit(0);
	}
}
