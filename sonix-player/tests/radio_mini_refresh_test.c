/* Actual radio state/poll/text functions; inert audio and label operations.
 * This fixture proves text/state propagation, not LVGL geometry or native audio.
 */
#include "src/system/playback/device_state.h"
#include "src/system/streaming/radio.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	char text[1024];
	bool hidden;
	const void *image;
} lv_obj_t;
enum { SX_MINI_NORMAL, SX_MINI_COMPACT };
static lv_obj_t slot_object, title_object, artist_object, line_object, icon_object;
static lv_obj_t full_title, full_artist;
static lv_obj_t *slot = &slot_object, *title = &title_object, *artist = &artist_object;
static lv_obj_t *compact_line = &line_object, *play_icon = &icon_object;
static lv_obj_t *song_title_label = &full_title, *song_artist_label = &full_artist;
static int mini_mode;
static bool compact_mode(void) { return mini_mode == SX_MINI_COMPACT; }
static bool has_track, live, playing;
static char title_text[512], artist_text[256], cover_shown_path[256];
static const int icon_stop = 1, icon_pause_small = 2, icon_play_small = 3;
static unsigned label_writes, frame_changes, progress_resets, full_refreshes, live_refreshes, cover_requests;
static unsigned output_begins, output_writes, output_ends;
static unsigned serial_getters;
static bool output_available = true;
static pthread_mutex_t now_lock = PTHREAD_MUTEX_INITIALIZER;
static radio_now_t now_state;
static radio_station_t now_station;
static unsigned now_serial;
static char current_metadata_file[512] = "/fixture/local.flac";
static song_metadata_t current_metadata;
static audio_status_t local_status = AUDIO_STATUS_PLAYING;

static void lv_label_set_text(lv_obj_t *o, const char *s) {
	label_writes++;
	snprintf(o->text, sizeof(o->text), "%s", s ? s : "");
}
static void lv_obj_set_hidden(lv_obj_t *o, bool hidden) { o->hidden = hidden; }
static void lv_image_set_src(lv_obj_t *o, const void *image) { o->image = image; }
static void sx_set_text(lv_obj_t *o, const char *s) {
	if (strcmp(o->text, s))
		lv_label_set_text(o, s);
}
static void sx_mini_set_progress(double a, double b) {
	(void)a;
	(void)b;
	progress_resets++;
}
static void sx_mini_update_visibility(void) {}
static void sx_frame_slot_changed(void) { frame_changes++; }
static void scrolltext_set(lv_obj_t *o, const char *s) { lv_label_set_text(o, s); }
static void lyrics_head_sync(void) {}
static void alt_pills_sync(void) {}
static void update_fav_button(void) {}
static void update_format_label(const device_state_t *s) {
	(void)s;
	live_refreshes++;
}
static void refresh_cover(const char *p) {
	cover_requests++;
	snprintf(cover_shown_path, sizeof(cover_shown_path), "%s", p);
}
static const char *tr(const char *key) { return !strcmp(key, "connecting") ? "Connecting" : key; }
const char *radio_station_problem(const radio_station_t *s) {
	(void)s;
	return NULL;
}
static void transport_stop(void) {}
static bool transport_start(void) { return true; }
static void recent_note(const radio_station_t *s) { (void)s; }
void playhistory_left(void) {}
void audio_stop(void) { local_status = AUDIO_STATUS_STOPPED; }
audio_status_t audio_get_status(void) { return local_status; }
void audio_get_current_file(char *s, size_t n) { snprintf(s, n, "%s", current_metadata_file); }
void audio_get_progress(double *a, double *b) {
	*a = 7;
	*b = 70;
}
bool device_state_scrub_active(double *s) {
	(void)s;
	return false;
}
void audio_get_stream_info(int *rate, int *channels) {
	*rate = 44100;
	*channels = 2;
}
static char *read_battery_percent(void) { return "80"; }
static bool read_battery_charging(void) { return true; }
static long get_volume_percent(void) { return 52; }
bool audio_external_begin(int rate, int channels, int bits) {
	assert(rate > 0 && channels == 2 && (bits == 16 || bits == 32));
	output_begins++;
	return output_available;
}
void audio_external_end(void) { output_ends++; }
int audio_external_write(const void *p, int frames) {
	assert(p && frames > 0);
	output_writes++;
	return frames;
}
static void eq_process(short *p, int frames, int channels, int rate) {
	(void)p;
	(void)frames;
	(void)channels;
	(void)rate;
}
static void eq_process_s32(int32_t *p, int frames, int channels, int rate) {
	(void)p;
	(void)frames;
	(void)channels;
	(void)rate;
}

#define device_state_get fixture_actual_device_state_get
#define radio_now_serial fixture_actual_radio_now_serial
#include "actual_functions.inc"
#undef radio_now_serial
#undef device_state_get

static bool open_after_snapshot;
static radio_out_t *interleave_out;
static short interleave_pcm[8], interleave_stereo[8];
void device_state_get(device_state_t *state) {
	fixture_actual_device_state_get(state);
	if (open_after_snapshot) {
		open_after_snapshot = false;
		assert(radio_push_pcm(interleave_out, interleave_pcm, interleave_stereo, 2, 44100, 2, 128, "MP3"));
	}
}
unsigned radio_now_serial(void) {
	serial_getters++;
	return fixture_actual_radio_now_serial();
}

/* Only initial whole-track paint is an adapter. Late updates run the actual
 * production revision block and actual refresh_live_texts()/mini setters.
 */
static void refresh_now_playing(void) {
	device_state_t state;
	device_state_get(&state);
	full_refreshes++;
	sx_mini_set_track(state.metadata.title, state.live ? NULL : state.current_file, state.metadata.artist, state.live);
	scrolltext_set(song_title_label, state.metadata.title);
	scrolltext_set(song_artist_label, state.metadata.artist);
}
static void poll_fixture(void) {
	unsigned before = serial_getters;
#include "actual_poll_block.inc"
#include "actual_poll_snapshot.inc"
	sx_mini_set_playing(state.status == AUDIO_STATUS_PLAYING);
	assert(serial_getters == before + 1);
}
static void expect_text(const char *name, const char *by) {
	assert(!strcmp(title_text, name));
	assert(!strcmp(artist_text, by));
	assert(!strcmp(full_title.text, name));
	assert(!strcmp(full_artist.text, by));
	if (mini_mode == SX_MINI_COMPACT) {
		char line[1024];
		snprintf(line, sizeof(line), "%s%s%s", name, *name && *by ? " \xC2\xB7 " : "", by);
		assert(!strcmp(line_object.text, line));
		assert(artist_object.hidden);
	} else {
		assert(artist_object.hidden == !*by);
		assert(!strcmp(artist_object.text, by));
	}
}
int main(int argc, char **argv) {
	assert(argc == 2);
	mini_mode = argv[1][0] == '1' ? SX_MINI_COMPACT : SX_MINI_NORMAL;
	snprintf(current_metadata.title, sizeof(current_metadata.title), "Local track");
	snprintf(current_metadata.artist, sizeof(current_metadata.artist), "Local artist");
	refresh_now_playing();
	poll_fixture();
	expect_text("Local track", "Local artist");
	radio_station_t station = {0};
	snprintf(station.name, sizeof(station.name), "Actual station");
	snprintf(station.url, sizeof(station.url), "http://fixture.invalid/no-network");
	assert(radio_play(&station));
	poll_fixture();
	expect_text("Actual station", "Connecting");
	unsigned resets = progress_resets, frames = frame_changes, full = full_refreshes;
	radio_out_t out = {0};
	short pcm[8] = {0}, stereo[8] = {0};
	int32_t wide[8] = {0}, wide_stereo[8] = {0};
	assert(radio_push_pcm(&out, pcm, stereo, 0, 44100, 2, 128, "MP3"));
	assert(now_state.connecting && !now_state.playing && !output_begins);
	output_available = false;
	assert(!radio_push_pcm(&out, pcm, stereo, 2, 44100, 2, 128, "MP3"));
	poll_fixture();
	expect_text("Actual station", "Connecting");
	assert(now_state.connecting && !now_state.playing);
	/* A successful real radio push occurs after the actual state snapshot but
	 * before the old poll's serial read. It must remain pending for next poll.
	 */
	output_available = true;
	interleave_out = &out;
	open_after_snapshot = true;
	poll_fixture();
	assert(!now_state.connecting && now_state.playing && output_writes == 1);
	poll_fixture();
	expect_text("Actual station", "");
	assert(full_refreshes == full && progress_resets == resets && frame_changes == frames);
	assert(live && has_track && icon_object.image == &icon_stop);
	unsigned paints = label_writes, refreshes = live_refreshes;
	for (int i = 0; i < 1000; i++)
		poll_fixture();
	assert(label_writes == paints && live_refreshes == refreshes && full_refreshes == full);
	pthread_mutex_lock(&now_lock);
	snprintf(now_state.title, sizeof(now_state.title), "Artist - ICY title");
	now_touch();
	pthread_mutex_unlock(&now_lock);
	poll_fixture();
	expect_text("Actual station", "Artist - ICY title");
	pthread_mutex_lock(&now_lock);
	snprintf(now_state.error, sizeof(now_state.error), "Connection failed");
	now_state.playing = false;
	now_touch();
	pthread_mutex_unlock(&now_lock);
	poll_fixture();
	expect_text("Actual station", "Connection failed");
	radio_stop();
	poll_fixture();
	expect_text("Actual station", "Connection failed");
	assert(icon_object.image == &icon_play_small);
	assert(radio_resume());
	poll_fixture();
	expect_text("Actual station", "Connecting");
	assert(!now_state.error[0]);
	assert(radio_push_pcm32(&out, wide, wide_stereo, 2, 48000, 2, "FLAC"));
	poll_fixture();
	expect_text("Actual station", "");
	assert(!now_state.connecting && now_state.playing && out.bits == 32);
	pthread_mutex_lock(&now_lock);
	snprintf(now_state.cover_path, sizeof(now_state.cover_path), "/fixture/station.png");
	now_touch();
	pthread_mutex_unlock(&now_lock);
	poll_fixture();
	assert(cover_requests == 1 && !strcmp(cover_shown_path, "/fixture/station.png"));
	expect_text("Actual station", "");
	frames = frame_changes;
	resets = progress_resets;
	paints = label_writes;
	refreshes = live_refreshes;
	for (int i = 0; i < 1000; i++)
		poll_fixture();
	assert(label_writes == paints && live_refreshes == refreshes && cover_requests == 1 && frame_changes == frames && progress_resets == resets);
	radio_stop();
	poll_fixture();
	expect_text("Actual station", "");
	assert(live && has_track && icon_object.image == &icon_play_small);
	snprintf(station.name, sizeof(station.name), "Other station");
	assert(radio_play(&station));
	poll_fixture();
	expect_text("Other station", "Connecting");
	assert(radio_push_pcm(&out, pcm, stereo, 2, 44100, 2, 128, "MP3"));
	poll_fixture();
	expect_text("Other station", "");
	radio_clear();
	poll_fixture();
	expect_text("Local track", "Local artist");
	assert(!live && has_track);
	paints = label_writes;
	refreshes = live_refreshes;
	for (int i = 0; i < 1000; i++) {
		now_touch();
		poll_fixture();
	}
	assert(label_writes == paints && live_refreshes == refreshes);
	printf("PASS mode=%d actual radio admission/open-state, live serial reconciliation, ICY/error/Stop/retry/cover/local/noop; inert audio+labels only\n", mini_mode);
}
