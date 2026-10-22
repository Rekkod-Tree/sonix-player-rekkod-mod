// Actual downloader recovery adapter + pair helper, synthetic folders only.
#define main prior_storage_barrier_main
#include "tests/podcast_download_storage_barrier_test.c"
#undef main

static char setup_folder[768];
static int setup_open(void *user) {
	(void)user;
	return open(setup_folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
static void digest_of(digest_t *d, const char *text) {
	sha256_ctx sha;
	sha256_init(&sha);
	sha256_update(&sha, text, strlen(text));
	sha256_final_hex(&sha, d->hash);
	d->present = true;
	d->size = (long long)strlen(text);
}
static void install_pending(const char *show) {
	char parent[700], p[1100];
	snprintf(parent, sizeof(parent), "%s/Podcast", fixture_root);
	assert(!mkdir(parent, 0700) || errno == EEXIST);
	snprintf(setup_folder, sizeof(setup_folder), "%s/%s", parent, show);
	assert(!mkdir(setup_folder, 0700) || (!show[0] && errno == EEXIST));
	record_t r = {0};
	snprintf(r.names[FINAL_A], 240, "Episode.mp3");
	snprintf(r.names[FINAL_T], 240, "Episode.mp3.tags");
	snprintf(r.names[STAGE_A], 240, ".Episode.mp3-55-1.part");
	snprintf(r.names[STAGE_T], 240, ".Episode.mp3.tags-55-2.tmp");
	char nonce[SHA256_HEX_LEN];
	sha256_ctx sha;
	sha256_init(&sha);
	sha256_update(&sha, r.names[STAGE_A], strlen(r.names[STAGE_A]));
	sha256_final_hex(&sha, nonce);
	snprintf(r.names[BACK_A], 240, PREFIX "%.32s.a.old", nonce);
	snprintf(r.names[BACK_T], 240, PREFIX "%.32s.t.old", nonce);
	digest_of(&r.data[OLD_A], "OLD-AUDIO");
	digest_of(&r.data[OLD_T], "OLD-TAGS");
	digest_of(&r.data[NEW_A], "NEW-AUDIO");
	digest_of(&r.data[NEW_T], "NEW-TAGS");
	const char *values[FILES] = {"NEW-AUDIO", "OLD-TAGS", NULL, "NEW-TAGS", "OLD-AUDIO", "OLD-TAGS"};
	for (int i = 0; i < FILES; i++)
		if (values[i]) {
			snprintf(p, sizeof(p), "%s/%s", setup_folder, r.names[i]);
			save(p, values[i]);
		}
	podcastpair_io_t io = {.open_dir = setup_open, .user = &io};
	file_t marker = {0};
	assert(schema(&r));
	assert(store_record(&io, MARKER, &r, &marker));
	snprintf(p, sizeof(p), "%s/Episode.mp3", setup_folder);
	assert(!podcastpair_ready(p));
	snprintf(p, sizeof(p), "%s/Other.mp3", setup_folder);
	save(p, "UNRELATED");
	assert(podcastpair_ready(p));
}
static void verify_old(const char *show) {
	char p[1100], text[32] = {0};
	snprintf(p, sizeof(p), "%s/Podcast/%s/Episode.mp3", fixture_root, show);
	FILE *f = fopen(p, "rb");
	assert(f);
	assert(fread(text, 1, sizeof(text) - 1, f) == 9);
	fclose(f);
	assert(!strcmp(text, "OLD-AUDIO"));
	assert(podcastpair_ready(p));
	snprintf(p, sizeof(p), "%s/Podcast/%s/Episode.mp3.tags", fixture_root, show);
	memset(text, 0, sizeof(text));
	f = fopen(p, "rb");
	assert(f);
	assert(fread(text, 1, sizeof(text) - 1, f) == 8);
	fclose(f);
	assert(!strcmp(text, "OLD-TAGS"));
}
static bool stop_recovery(void *limit) { return pin_calls > (int)(intptr_t)limit; }
static unsigned recovery_epoch;
static podcastpair_result_t recovery_result;
static void *run_recovery(void *unused) {
	(void)unused;
	recovery_result = podcastdl_recover_card(recovery_epoch, NULL, NULL);
	return NULL;
}
int main(void) {
	setbuf(stdout, NULL);
	alarm(45);
	char base[] = "/tmp/sonix-pair-adapter-XXXXXX";
	assert(mkdtemp(base));
	snprintf(fixture_root, sizeof(fixture_root), "%s/normal", base);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	gate_target = PHASE_NONE;
	install_pending("First");
	install_pending("Second");
	install_pending("");
	pin_calls = 0;
	int before = countfds();
	assert(podcastdl_recover_card(fixture_epoch, NULL, NULL) == PODCASTPAIR_OK);
	verify_old("First");
	verify_old("Second");
	verify_old("");
	assert(countfds() == before && !podcastpair_busy());
	printf("PASS local recovery root and two folders, descriptor balance, unaffected item ready, pins=%d\n", pin_calls);
	snprintf(fixture_root, sizeof(fixture_root), "%s/cancel", base);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	install_pending("First");
	pin_calls = 0;
	assert(podcastdl_recover_card(fixture_epoch, stop_recovery, (void *)8) == PODCASTPAIR_RECOVERY);
	assert(countfds() == before && !podcastpair_busy());
	assert(podcastdl_recover_card(fixture_epoch - 1, NULL, NULL) == PODCASTPAIR_RECOVERY);
	assert(podcastdl_recover_card(fixture_epoch, NULL, NULL) == PODCASTPAIR_OK);
	verify_old("First");
	puts("PASS cancellation releases RAM owner and descriptors, stale queued epoch refused");
	snprintf(fixture_root, sizeof(fixture_root), "%s/replacement", base);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	install_pending("First");
	pin_calls = gate_at = gate_go = 0;
	gate_target = PHASE_PIN;
	recovery_epoch = fixture_epoch;
	pthread_t t;
	assert(!pthread_create(&t, NULL, run_recovery, NULL));
	await_gate();
	replace_root();
	release_gate();
	assert(!pthread_join(t, NULL));
	verify_replacement();
	assert(recovery_result == PODCASTPAIR_RECOVERY && countfds() == before && !podcastpair_busy());
	puts("PASS old recovery cannot mutate replacement card");
	snprintf(fixture_root, sizeof(fixture_root), "%s/show-swap", base);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	gate_target = PHASE_NONE;
	install_pending("Show");
	pin_calls = gate_at = gate_go = 0;
	gate_target = PHASE_PUBLISH;
	recovery_epoch = fixture_epoch;
	assert(!pthread_create(&t, NULL, run_recovery, NULL));
	await_gate();
	char show[800], old[800], p[1100];
	snprintf(show, sizeof(show), "%s/Podcast/Show", fixture_root);
	snprintf(old, sizeof(old), "%s/Podcast/Old", fixture_root);
	assert(!rename(show, old));
	assert(!mkdir(show, 0700));
	DIR *d = opendir(old);
	assert(d);
	struct dirent *e;
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
			snprintf(p, sizeof(p), "%s/%s", show, e->d_name);
			save(p, "REPLACEMENT-SENTINEL");
		}
	closedir(d);
	release_gate();
	assert(!pthread_join(t, NULL));
	verify_replacement();
	assert(recovery_result == PODCASTPAIR_RECOVERY && countfds() == before && !podcastpair_busy());
	puts("PASS same-card show replacement cannot redirect old record recovery");
	return 0;
}
