// Full application/audio loops, ALSA null plus existing HOST_BUILD pacing.
// Opt in through run_graphite_ui.sh with disposable configuration/card roots.
static void pause_wait_unexpected_eof(void) { assert(!"Explicit transport reported natural EOF"); }

static void pause_wait_states(void) {
	static int stage, format, waits, short_retries;
	static char wav[1024];
	static double before;
	static unsigned serial, parked_turns;
	static uint32_t paused_at, parked_at;
	double current = 0, total = 0;
	unsigned observed_serial = 0;
	bool actual = audio_get_listening_progress(&current, &total, &observed_serial);
	const char *path = format == 0 ? wav : getenv("SONIX_UI_TEST_TRACK");
	assert(++waits < 30); // Bounded condition retries, not exact scheduling latency.
	assert(!audio_take_completion()); // None of these explicit transports is EOF.
	switch (stage) {
	case 0:
		gui_init_rest();
		audio_set_completion_hook(pause_wait_unexpected_eof);
		snprintf(wav, sizeof(wav), "%s/Graphite/Pause-WAV.wav", getenv("SONIX_SD_ROOT"));
		assert(!access(wav, R_OK));
		assert(library_scan_start(getenv("SONIX_SD_ROOT")));
		char books[1024];
		snprintf(books, sizeof(books), "%s/Audiobooks", getenv("SONIX_SD_ROOT"));
		assert(audiobookdb_scan_start(books));
		stage = 1;
		break;
	case 1:
		if (library_scan_running() || audiobookdb_scan_running()) return;
		assert(audiobookdb_count() > 0);
		audio_set_gapless(false);
		assert(audio_play(path) == 0);
		stage = 2;
		break;
	case 2:
		if (!actual || current < .25 || audio_get_status() != AUDIO_STATUS_PLAYING) return;
		assert(audio_playback_context_active() && audio_device_is_open() && total > 40);
		serial = observed_serial;
		audio_pause();
		paused_at = lv_tick_get();
		stage = 3;
		break;
	case 3:
		if (audio_get_status() != AUDIO_STATUS_PAUSED || !actual) return;
		assert(audio_playback_context_active());
		// Retry a short pause if the entire host was descheduled past its window.
		if (lv_tick_elaps(paused_at) >= 4500) {
			assert(++short_retries < 4);
			audio_resume(); stage = 2; break;
		}
		assert(audio_device_is_open());
		before = current;
		audio_resume();
		stage = 4;
		break;
	case 4:
		if (!actual || audio_get_status() != AUDIO_STATUS_PLAYING || current <= before + .25) return;
		assert(observed_serial == serial && audio_device_is_open());
		audio_pause();
		paused_at = lv_tick_get();
		stage = 5;
		break;
	case 5:
		if (audio_get_status() != AUDIO_STATUS_PAUSED || audio_device_is_open()) return;
		assert(lv_tick_elaps(paused_at) >= 4500 && audio_playback_context_active());
		parked_at = lv_tick_get();
		parked_turns = audio_loop_turns();
		stage = 6;
		break;
	case 6:
		assert(audio_get_status() == AUDIO_STATUS_PAUSED && !audio_device_is_open());
		assert(audio_playback_context_active());
		if (lv_tick_elaps(parked_at) < 2500) return;
		printf("PAUSE_WAIT %s parked_ms=%u loop_delta=%u\n", format ? "FLAC" : "WAV",
			(unsigned)lv_tick_elaps(parked_at), audio_loop_turns() - parked_turns);
		assert(audio_loop_turns() - parked_turns <= 2); // Old 5 Hz polling fails this.
		capture(format ? "pause-wait-flac-parked" : "pause-wait-wav-parked");
		audio_seek(12.0);
		stage = 7;
		break;
	case 7:
		// Unfloored engine cursor proves the paused loop serviced the seek.
		if (current < 11.9 || current > 12.1) return;
		assert(!actual && audio_get_status() == AUDIO_STATUS_PAUSED && !audio_device_is_open());
		audio_resume();
		stage = 8;
		break;
	case 8:
		if (!actual || current < 12.25 || audio_get_status() != AUDIO_STATUS_PLAYING) return;
		assert(observed_serial != serial && audio_device_is_open());
		audio_stop();
		stage = 9;
		break;
	case 9:
		if (audio_playback_context_active() || audio_device_is_open()) return;
		assert(audio_get_status() == AUDIO_STATUS_STOPPED);
		printf("UI_TEST_PASS full %s loop short/long pause, parked wait, paused seek, Resume and Stop\n", format ? "FLAC" : "WAV");
		if (++format < 2) { assert(audio_play(getenv("SONIX_UI_TEST_TRACK")) == 0); stage = 2; }
		else { player_play_file(getenv("SONIX_UI_TEST_BOOK")); stage = 10; }
		break;
	case 10:
		if (!actual || current < .25 || !audiobook_is_playing()) return;
		assert(audio_playback_context_active());
		audio_pause();
		stage = 11;
		break;
	case 11:
		if (audio_get_status() != AUDIO_STATUS_PAUSED) return;
		assert(audiobook_is_playing() && audio_playback_context_active());
		player_play_file(getenv("SONIX_UI_TEST_NEXT"));
		stage = 12;
		break;
	case 12: {
		char loaded[1024];
		audio_get_current_file(loaded, sizeof(loaded));
		if (!actual || current < .25 || strcmp(loaded, getenv("SONIX_UI_TEST_NEXT")) || audiobook_is_playing()) return;
		assert(audio_get_status() == AUDIO_STATUS_PLAYING && audio_playback_context_active());
		audio_stop();
		stage = 13;
		break;
	}
	case 13:
		if (audio_playback_context_active() || audio_device_is_open()) return;
		assert(audio_get_status() == AUDIO_STATUS_STOPPED);
		capture("pause-wait-final-stopped");
		puts("UI_TEST_PASS book FLAC paused context replaced by music; no false completion; clean Stop");
		fflush(stdout);
		exit(0);
	}
	waits = 0;
}
