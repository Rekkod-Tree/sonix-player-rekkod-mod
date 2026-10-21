// cc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -I. tests/podcast_state_ordering_test.c build_host/system/db/sqlite3_impl.o -Wl,--gc-sections -pthread -ldl -lm -o /tmp/podcast_order
#define _GNU_SOURCE
#include "src/system/streaming/podcaststate.c"
#include <assert.h>
#include <stdatomic.h>
void thread_be_background(const char *s) { (void)s; }
bool podcastdl_owns(const char *path) { return strstr(path, "/fixture/") != NULL; }
void file_identity_read(const char *path, file_identity_t *out) {
	struct stat st;
	*out = (file_identity_t){0};
	if (!stat(path, &st)) {
		out->exists = true;
		out->dev = st.st_dev;
		out->ino = st.st_ino;
	}
}
bool file_identity_changed(const char *path, const file_identity_t *old) {
	file_identity_t now;
	file_identity_read(path, &now);
	return now.exists != old->exists || now.dev != old->dev || now.ino != old->ino;
}
static podcast_episode_t episode = {.id = 1, .duration_secs = 600};
static _Atomic bool finished;
static void *mark(void *unused) {
	(void)unused;
	podcaststate_set_played_episode(&episode, "", true);
	finished = true;
	return NULL;
}
static void *drain(void *unused) {
	(void)unused;
	writer_drain();
	finished = true;
	return NULL;
}
static char second[512];
static void *detach(void *unused) {
	(void)unused;
	podcaststate_set_root(second);
	finished = true;
	return NULL;
}
static void wait_busy(void) {
	for (;;) {
		pthread_mutex_lock(&writer_lock);
		bool busy = writer_busy;
		pthread_mutex_unlock(&writer_lock);
		if (busy)
			break;
		usleep(1000);
	}
}
static void remove_db(const char *root) {
	char path[640];
	snprintf(path, sizeof(path), "%s/.local/podcast.db", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/.local/podcast.db-journal", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/.local", root);
	rmdir(path);
	rmdir(root);
}
int main(void) {
	alarm(15);
	char first[] = "/tmp/sonix-podcast-a-XXXXXX", next[] = "/tmp/sonix-podcast-b-XXXXXX";
	assert(mkdtemp(first) && mkdtemp(next));
	snprintf(second, sizeof(second), "%s", next);
	podcaststate_set_root(first);
	unsigned old_epoch = podcaststate_card_epoch();
	podcaststate_t record = {.episode_id = 1, .duration = 600, .position = 12};
	write_now("e:1", &record, old_epoch);
	pthread_mutex_lock(&db_lock);
	writer_post("e:1", &record, old_epoch);
	wait_busy();
	pthread_t waiter;
	finished = false;
	assert(!pthread_create(&waiter, NULL, drain, NULL));
	usleep(20000);
	assert(!finished);
	pthread_mutex_unlock(&db_lock);
	pthread_join(waiter, NULL);
	// Pause an already dequeued worker inside its DB acquisition.
	pthread_mutex_lock(&db_lock);
	writer_post("e:1", &record, old_epoch);
	wait_busy();
	pthread_t t;
	finished = false;
	assert(!pthread_create(&t, NULL, mark, NULL));
	usleep(20000);
	assert(!finished);
	pthread_mutex_unlock(&db_lock);
	pthread_join(t, NULL);
	writer_drain();
	podcaststate_t state;
	assert(podcaststate_get_episode(1, &state) && state.played && state.position == 0);
	// A root transition must wait for an in-flight write, then reject old jobs.
	pthread_mutex_lock(&db_lock);
	writer_post("e:1", &record, old_epoch);
	wait_busy();
	finished = false;
	assert(!pthread_create(&t, NULL, detach, NULL));
	usleep(20000);
	assert(!finished);
	pthread_mutex_unlock(&db_lock);
	pthread_join(t, NULL);
	writer_post("e:1", &record, old_epoch);
	write_now("e:1", &record, old_epoch);
	writer_drain();
	assert(!podcaststate_get_episode(1, &state));
	podcaststate_set_played_episode(&episode, "", false);
	podcaststate_downloaded(1, "/fixture/old-card.flac", old_epoch);
	assert(podcaststate_get_episode(1, &state) && !state.path[0]);
	podcaststate_downloaded(1, "/fixture/current-card.flac", podcaststate_card_epoch());
	assert(podcaststate_get_episode(1, &state) && !strcmp(state.path, "/fixture/current-card.flac"));
	// Different episode keys must not coalesce away one another.
	unsigned epoch = podcaststate_card_epoch();
	for (int i = 1; i <= 40; i++) {
		char key[32];
		snprintf(key, sizeof(key), "e:%d", i);
		record.episode_id = i;
		writer_post(key, &record, epoch);
	}
	writer_drain();
	for (int i = 1; i <= 40; i++)
		assert(podcaststate_get_episode(i, &state) && state.position == 12);
	podcaststate_set_root(NULL);
	remove_db(first);
	remove_db(second);
	puts("PASS podcast in-flight drain, forced state, old-card jobs and cross-episode queue");
}
