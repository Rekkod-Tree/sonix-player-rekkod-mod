// Real downloader native-sidecar guard; no service/device/network.
#define main old_barrier_main
#include "tests/podcast_download_storage_barrier_test.c"
#undef main
static void exact(const char *path, const char *value) {
	FILE *f = fopen(path, "rb");
	assert(f);
	char buf[128] = {0};
	assert(fread(buf, 1, sizeof(buf) - 1, f) == strlen(value));
	fclose(f);
	assert(!strcmp(buf, value));
}
int main(void) {
	alarm(30);
	setbuf(stdout, NULL);
	char tmp[] = "/tmp/sonix-native-id-XXXXXX";
	assert(mkdtemp(tmp));
	snprintf(fixture_root, sizeof(fixture_root), "%s", tmp);
	fixture_epoch++;
	char show[700], audio[800], tags[840];
	snprintf(show, sizeof(show), "%s/Podcast", tmp);
	assert(!mkdir(show, 0700));
	snprintf(show, sizeof(show), "%s/Podcast/Show", tmp);
	assert(!mkdir(show, 0700));
	snprintf(audio, sizeof(audio), "%s/Episode.mp3", show);
	snprintf(tags, sizeof(tags), "%s.tags", audio);
	save(audio, "KEEP-AUDIO");
	podcast_episode_t e = {.id = 7};
	snprintf(e.title, sizeof(e.title), "Episode");
	snprintf(e.enclosure, sizeof(e.enclosure), "fixture://body");
	assert(podcastdl_exists(&e, "Show"));
	save(tags, "title=Manual\nartist=User\n");
	assert(podcastdl_exists(&e, "Show"));
	save(tags, "episode_id=7\n");
	assert(podcastdl_exists(&e, "Show"));
	save(tags, "episode_id=7");
	assert(podcastdl_exists(&e, "Show"));
	const char *bad[] = {"episode_id=8\n", "episode_id=bad\n", "episode_id=-1\n", "episode_id=0\n", "episode_id=184467440737095516160\n", "episode_id=7junk\n", "episode_id=7\nepisode_id=7\n"};
	gate_target = PHASE_NONE;
	baseline_fds = countfds();
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		save(tags, bad[i]);
		assert(!podcastdl_exists(&e, "Show"));
		int calls = http_opens;
		assert(podcastdl_start(&e, "Show", "Author", NULL));
		assert(!pthread_join(worker_id, NULL));
		assert(http_opens == calls && countfds() == baseline_fds && !podcastpair_busy());
		assert(podcastdl_state(NULL, NULL) == PODCASTDL_RECOVERY_REQUIRED);
		exact(audio, "KEEP-AUDIO");
		exact(tags, bad[i]);
		podcastdl_acknowledge();
	}
	FILE *binary = fopen(tags, "wb");
	assert(binary);
	static const char nul_tag[] = "episode_id=7\0junk\n";
	assert(fwrite(nul_tag, 1, sizeof(nul_tag) - 1, binary) == sizeof(nul_tag) - 1);
	assert(!fclose(binary));
	assert(!podcastdl_exists(&e, "Show"));
	int calls = http_opens;
	assert(podcastdl_start(&e, "Show", "Author", NULL));
	assert(!pthread_join(worker_id, NULL));
	assert(http_opens == calls && countfds() == baseline_fds && !podcastpair_busy());
	assert(podcastdl_state(NULL, NULL) == PODCASTDL_RECOVERY_REQUIRED);
	exact(audio, "KEEP-AUDIO");
	podcastdl_acknowledge();
	puts("PASS manual/native ID match, mismatch, malformed, overflow, duplicate; foreign native pair untouched without HTTP");
	assert(!unlink(audio) && !unlink(tags));
	body_reads = 0;
	fail_fsync = fail_rename = 0;
	assert(podcastdl_start(&e, "Show", "Author\nepisode_id=99", NULL));
	assert(!pthread_join(worker_id, NULL));
	assert(podcastdl_state(NULL, NULL) == PODCASTDL_DONE && podcastdl_exists(&e, "Show"));
	FILE *f = fopen(tags, "rb");
	assert(f);
	char buf[4096] = {0};
	assert(fread(buf, 1, sizeof(buf) - 1, f) > 0);
	fclose(f);
	assert(strstr(buf, "feed_author=Author episode_id=99\n") && strstr(buf, "\nepisode_id=7\n"));
	assert(countfds() == baseline_fds && !podcastpair_busy());
	puts("PASS untrusted metadata newlines cannot create extra native ID fields");
	return 0;
}
