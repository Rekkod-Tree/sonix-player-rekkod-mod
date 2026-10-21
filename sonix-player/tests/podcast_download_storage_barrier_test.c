/* Production downloader plus deterministic two-thread filesystem barriers.
 * From sonix-player (Linux), no device/network or external dependencies:
 * cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   -fno-omit-frame-pointer -ffunction-sections -fdata-sections -I. \
 *   tests/podcast_download_storage_barrier_test.c -Wl,--gc-sections -pthread \
 *   -o /tmp/podcast-storage-barrier-test
 * ASAN_OPTIONS=detect_leaks=1 /tmp/podcast-storage-barrier-test
 *
 * All fixtures use fresh /tmp directories and are printed for inspection.
 * Network waits must own no filesystem descriptors. Replacing a root with
 * matching staging sentinels must never redirect writes or cleanup into it.
 * Fault case rename=2 documents the known non-atomic audio/sidecar publication
 * tracked as backlog #30; its assertion describes a residual, not a fix.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
enum { PHASE_NONE, PHASE_OPEN, PHASE_READ2, PHASE_COVER, PHASE_PIN, PHASE_TAG_RENAME, PHASE_PUBLISH, PHASE_COVER_RENAME, PHASE_CLEANUP, PHASE_WRITE };
static void gate(int);
static int countfds(void);
static int baseline_fds;
static int fail_rename, fail_fsync, fsync_calls, pin_calls, hold_epoch;
static int fixture_renameat(int a, const char *b, int c, const char *d) {
	gate(strstr(b, "cover") ? PHASE_COVER_RENAME : strstr(d, ".tags") ? PHASE_TAG_RENAME : PHASE_PUBLISH);
	if (fail_rename && ((fail_rename == 1 && !strstr(d, ".tags") && !strstr(b, "cover")) || (fail_rename == 2 && strstr(d, ".tags")))) {
		errno = EIO;
		return -1;
	}
	return renameat(a, b, c, d);
}
static int fixture_unlinkat(int a, const char *b, int c) {
	gate(PHASE_CLEANUP);
	return unlinkat(a, b, c);
}
static ssize_t fixture_write(int a, const void *b, size_t c) {
	gate(PHASE_WRITE);
	return write(a, b, c);
}
static pthread_t worker_id;
static int fixture_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
	(void)a;
	int rc = pthread_create(t, NULL, fn, arg);
	if (!rc)
		worker_id = *t;
	return rc;
}
#define pthread_create fixture_create
static int fixture_fsync(int fd) {
	if (++fsync_calls == fail_fsync) {
		errno = EIO;
		return -1;
	}
	return fsync(fd);
}
#define fsync fixture_fsync
#define renameat fixture_renameat
#define unlinkat fixture_unlinkat
#define write fixture_write
#include "src/system/streaming/podcastdl.c"
#undef pthread_create
#undef fsync
#undef renameat
#undef unlinkat
#undef write
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static int gate_target, gate_at, gate_go, body_reads;
static char fixture_root[512];
static _Atomic unsigned fixture_epoch;
static unsigned accepted, refused;

static void gate(int at) {
	if (at == PHASE_OPEN || at == PHASE_READ2 || at == PHASE_COVER)
		assert(countfds() == baseline_fds);
	pthread_mutex_lock(&gate_lock);
	if (gate_target == at) {
		gate_at = at;
		pthread_cond_broadcast(&gate_cv);
		while (!gate_go)
			pthread_cond_wait(&gate_cv, &gate_lock);
	}
	pthread_mutex_unlock(&gate_lock);
}
static void await_gate(void) {
	pthread_mutex_lock(&gate_lock);
	while (!gate_at)
		pthread_cond_wait(&gate_cv, &gate_lock);
	pthread_mutex_unlock(&gate_lock);
}
static void release_gate(void) {
	pthread_mutex_lock(&gate_lock);
	gate_go = 1;
	pthread_cond_broadcast(&gate_cv);
	pthread_mutex_unlock(&gate_lock);
}
const char *storage_sd_root(void) { return fixture_root; }
int storage_pin_card(char *out, size_t size) {
	snprintf(out, size, "%s", fixture_root);
	int fd = open(fixture_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (++pin_calls > 1)
		gate(PHASE_PIN);
	return fd;
}
void thread_be_low_priority(const char *s) { (void)s; }
unsigned podcaststate_card_epoch(void) { return fixture_epoch; }
void podcaststate_downloaded(long long id, const char *path, unsigned epoch) {
	(void)id;
	(void)path;
	if (epoch == fixture_epoch)
		accepted++;
	else
		refused++;
}
bool http_stream_open(http_stream_t *s, const char *u, int secs) {
	(void)u;
	(void)secs;
	s->content_length = 8;
	gate(PHASE_OPEN);
	return true;
}
int http_stream_read(http_stream_t *s, void *b, int len) {
	(void)s;
	(void)len;
	if (body_reads == 1)
		gate(PHASE_READ2);
	if (body_reads == 1 && gate_target == PHASE_CLEANUP)
		return -1;
	if (body_reads++ >= 2)
		return 0;
	memcpy(b, "TEST", 4);
	return 4;
}
void http_stream_close(http_stream_t *s) { (void)s; }
void http_stream_wake(http_stream_t *s) { (void)s; }
bool http_get(const char *u, char **out, size_t *len, size_t cap, int secs) {
	(void)u;
	(void)cap;
	(void)secs;
	gate(PHASE_COVER);
	*out = malloc(8);
	memcpy(*out, "\x89PNGTEST", 8);
	*len = 8;
	return true;
}
static void save(const char *p, const char *data) {
	FILE *f = fopen(p, "wb");
	assert(f);
	assert(fwrite(data, 1, strlen(data), f) == strlen(data));
	assert(!fclose(f));
}
static int countfds(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	while (readdir(d))
		n++;
	closedir(d);
	return n;
}
static void replace_root(void) {
	char old[600], parent[600], show[600], entry[900];
	snprintf(old, sizeof(old), "%s-old", fixture_root);
	assert(!rename(fixture_root, old));
	if (!hold_epoch)
		fixture_epoch++;
	assert(!mkdir(fixture_root, 0700));
	snprintf(parent, sizeof(parent), "%s/Podcast", fixture_root);
	assert(!mkdir(parent, 0700));
	snprintf(show, sizeof(show), "%s/Podcast/Show", fixture_root);
	assert(!mkdir(show, 0700));
	char oldshow[700];
	snprintf(oldshow, sizeof(oldshow), "%s/Podcast/Show", old);
	DIR *d = opendir(oldshow);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)))
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
				snprintf(entry, sizeof(entry), "%s/%s", show, e->d_name);
				save(entry, "REPLACEMENT-SENTINEL");
			}
		closedir(d);
	}
	snprintf(entry, sizeof(entry), "%s/keep", show);
	save(entry, "REPLACEMENT-SENTINEL");
}
static void verify_replacement(void) {
	char show[600], path[900];
	snprintf(show, sizeof(show), "%s/Podcast/Show", fixture_root);
	DIR *d = opendir(show);
	assert(d);
	struct dirent *e;
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
			snprintf(path, sizeof(path), "%s/%s", show, e->d_name);
			FILE *f = fopen(path, "rb");
			assert(f);
			char b[40] = {0};
			size_t n = fread(b, 1, sizeof(b) - 1, f);
			fclose(f);
			assert(n == 20 && !strcmp(b, "REPLACEMENT-SENTINEL"));
		}
	closedir(d);
}
static void scenario(const char *base, int phase, bool cancel) {
	snprintf(fixture_root, sizeof(fixture_root), "%s/case-%d-%d", base, phase, cancel);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	accepted = refused = 0;
	body_reads = 0;
	pin_calls = 0;
	gate_at = gate_go = 0;
	gate_target = phase;
	podcast_episode_t episode = {.id = 7, .feed_id = 8, .published = 12345};
	snprintf(episode.title, sizeof(episode.title), "Episode");
	snprintf(episode.feed_title, sizeof(episode.feed_title), "Show");
	snprintf(episode.enclosure, sizeof(episode.enclosure), "fixture://body");
	snprintf(episode.image, sizeof(episode.image), "fixture://cover");
	int before = countfds();
	baseline_fds = before;
	assert(podcastdl_start(&episode, "Show", "Author", "fixture://cover"));
	if (phase != PHASE_NONE) {
		await_gate();
		assert(!podcastdl_start(&episode, "Show", "Author", "fixture://cover"));
		if (cancel)
			podcastdl_cancel();
		else
			replace_root();
		release_gate();
	}
	assert(!pthread_join(worker_id, NULL));
	int after = countfds();
	assert(before == after);
	if (phase != PHASE_NONE && !cancel)
		verify_replacement();
	if (phase == PHASE_NONE) {
		char p[700];
		snprintf(p, sizeof(p), "%s/Podcast/Show/Episode.mp3", fixture_root);
		FILE *f = fopen(p, "rb");
		assert(f);
		char b[9] = {0};
		assert(fread(b, 1, 8, f) == 8);
		fclose(f);
		assert(!strcmp(b, "TESTTEST"));
		assert(accepted == 1);
		snprintf(p, sizeof(p), "%s/Podcast/Show/Episode.mp3.tags", fixture_root);
		assert(!access(p, F_OK));
		snprintf(p, sizeof(p), "%s/Podcast/Show/cover.png", fixture_root);
		assert(!access(p, F_OK));
	}
	if (phase != PHASE_COVER && phase != PHASE_COVER_RENAME && phase != PHASE_PUBLISH && phase != PHASE_NONE)
		assert(accepted == 0);
	printf("PASS phase=%d cancel=%d fd=%d state=%d accepted=%u refused=%u\n", phase, cancel, after, podcastdl_state(NULL, NULL), accepted, refused);
	podcastdl_acknowledge();
}
static void replaced_part(const char *base, bool link) {
	snprintf(fixture_root, sizeof(fixture_root), "%s/part-%d", base, link);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	accepted = refused = 0;
	body_reads = 0;
	pin_calls = 0;
	gate_at = gate_go = 0;
	gate_target = PHASE_READ2;
	podcast_episode_t e = {.id = 7};
	snprintf(e.title, sizeof(e.title), "Episode");
	snprintf(e.enclosure, sizeof(e.enclosure), "fixture://body");
	baseline_fds = countfds();
	assert(podcastdl_start(&e, "Show", "Author", NULL));
	await_gate();
	char show[600], part[900] = {0}, target[900], tmp[900];
	snprintf(show, sizeof(show), "%s/Podcast/Show", fixture_root);
	DIR *d = opendir(show);
	assert(d);
	struct dirent *ent;
	while ((ent = readdir(d))) {
		if (strstr(ent->d_name, ".part")) {
			assert(!part[0]);
			snprintf(part, sizeof(part), "%s/%s", show, ent->d_name);
		}
	}
	closedir(d);
	assert(part[0]);
	snprintf(target, sizeof(target), "%s/target", fixture_root);
	snprintf(tmp, sizeof(tmp), "%s/replacement", fixture_root);
	save(target, "REPLACEMENT-SENTINEL");
	if (link)
		assert(!symlink(target, tmp));
	else
		save(tmp, "REPLACEMENT-SENTINEL");
	assert(!rename(tmp, part));
	release_gate();
	assert(!pthread_join(worker_id, NULL));
	FILE *f = fopen(part, "rb");
	assert(f);
	char b[40] = {0};
	assert(fread(b, 1, 20, f) == 20);
	fclose(f);
	assert(!strcmp(b, "REPLACEMENT-SENTINEL"));
	assert(accepted == 0);
	assert(countfds() == baseline_fds);
	char final[700];
	snprintf(final, sizeof(final), "%s/Episode.mp3", show);
	assert(access(final, F_OK) != 0);
	printf("PASS replaced staging symlink=%d preserved\n", link);
	podcastdl_acknowledge();
}
static void fault_case(const char *base, int renamefail, int syncfail) {
	snprintf(fixture_root, sizeof(fixture_root), "%s/fault-%d-%d", base, renamefail, syncfail);
	assert(!mkdir(fixture_root, 0700));
	fixture_epoch++;
	accepted = refused = 0;
	body_reads = 0;
	pin_calls = 0;
	gate_at = gate_go = 0;
	gate_target = PHASE_OPEN;
	fail_rename = renamefail;
	fail_fsync = syncfail;
	fsync_calls = 0;
	podcast_episode_t e = {.id = 7};
	snprintf(e.title, sizeof(e.title), "Episode");
	snprintf(e.enclosure, sizeof(e.enclosure), "fixture://body");
	baseline_fds = countfds();
	assert(podcastdl_start(&e, "Show", "Author", "fixture://cover"));
	await_gate();
	char p[700], show[600], tags[700];
	snprintf(show, sizeof(show), "%s/Podcast", fixture_root);
	assert(!mkdir(show, 0700));
	snprintf(show, sizeof(show), "%s/Podcast/Show", fixture_root);
	assert(!mkdir(show, 0700));
	snprintf(p, sizeof(p), "%s/Episode.mp3", show);
	snprintf(tags, sizeof(tags), "%s/Episode.mp3.tags", show);
	save(p, "OLD-AUDIO");
	save(tags, "OLD-TAGS");
	release_gate();
	assert(!pthread_join(worker_id, NULL));
	FILE *f = fopen(p, "rb");
	assert(f);
	char b[30] = {0};
	assert(fread(b, 1, sizeof(b) - 1, f) > 0);
	fclose(f);
	FILE *t = fopen(tags, "rb");
	assert(t);
	char c[30] = {0};
	assert(fread(c, 1, sizeof(c) - 1, t) > 0);
	fclose(t);
	printf("FAULT rename=%d fsync=%d audio=%s tags=%s state=%d notice=%u\n", renamefail, syncfail, b, c, podcastdl_state(NULL, NULL), accepted);
	if (renamefail == 1 || syncfail == 1 || syncfail == 2) {
		assert(!strcmp(b, "OLD-AUDIO"));
		assert(!strcmp(c, "OLD-TAGS"));
		assert(!accepted);
	}
	/* Known #30 residual: a second rename failure does not roll back audio. */
	if (renamefail == 2) {
		assert(!strcmp(b, "TESTTEST"));
		assert(!strcmp(c, "OLD-TAGS"));
		assert(!accepted);
		assert(podcastdl_state(NULL, NULL) == PODCASTDL_FAILED);
	}
	assert(countfds() == baseline_fds);
	podcastdl_acknowledge();
	fail_rename = fail_fsync = 0;
}
int main(void) {
	setbuf(stdout, NULL);
	alarm(45);
	char base[] = "/tmp/sonix-safety29-independent-XXXXXX";
	assert(mkdtemp(base));
	scenario(base, PHASE_NONE, false);
	scenario(base, PHASE_OPEN, false);
	scenario(base, PHASE_PIN, false);
	scenario(base, PHASE_READ2, false);
	scenario(base, PHASE_COVER, false);
	scenario(base, PHASE_TAG_RENAME, false);
	scenario(base, PHASE_PUBLISH, false);
	scenario(base, PHASE_COVER_RENAME, false);
	scenario(base, PHASE_CLEANUP, false);
	scenario(base, PHASE_WRITE, false);
	scenario(base, PHASE_OPEN, true);
	scenario(base, PHASE_READ2, true);
	scenario(base, PHASE_COVER, true);
	replaced_part(base, false);
	replaced_part(base, true);
	fault_case(base, 1, 0);
	fault_case(base, 2, 0);
	fault_case(base, 0, 1);
	fault_case(base, 0, 2);
	char other[] = "/tmp/sonix-safety29-identity-XXXXXX";
	assert(mkdtemp(other));
	hold_epoch = 1;
	scenario(other, PHASE_OPEN, false);
	scenario(other, PHASE_READ2, false);
	printf("fixtures=%s identity=%s\n", base, other);
	return 0;
}
