/* Production root/path/owns and state epoch test; no real card/network.
From sonix-player (Linux), use a private output directory:
cc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -I. tests/podcast_root_ownership_test.c \
  src/system/streaming/podcastdl.c src/system/core/utils.c src/system/db/sqlite3_impl.c \
  -Wl,--gc-sections -pthread -ldl -lm -o <output>/podcast_root_test
ASAN_OPTIONS=detect_leaks=1 <output>/podcast_root_test
*/
#define _GNU_SOURCE
#include "src/system/streaming/podcaststate.c"
#include <assert.h>
#include <stdatomic.h>

static _Thread_local const char *thread_root;
static _Atomic(const char *) active_root;
static pthread_barrier_t rendezvous;
const char *storage_sd_root(void) { return thread_root ? thread_root : atomic_load(&active_root); }

static void *concurrent_paths(void *arg) {
	thread_root = arg;
	char want[768], want_root[512];
	snprintf(want_root, sizeof(want_root), "%s/Podcast", thread_root);
	snprintf(want, sizeof(want), "%s/Show/Episode.mp3", want_root);
	podcast_episode_t episode = {.id = 9};
	snprintf(episode.title, sizeof(episode.title), "Episode");
	for (int i = 0; i < 2000; i++) {
		char root[512], path[768];
		pthread_barrier_wait(&rendezvous);
		assert(podcastdl_root(root, sizeof(root)));
		assert(podcastdl_path(&episode, "Show", path, sizeof(path)));
		pthread_barrier_wait(&rendezvous);
		// Other calls cannot mutate either caller-owned result after return.
		assert(!strcmp(root, want_root) && !strcmp(path, want));
		assert(podcastdl_owns(want));
		assert(!podcastdl_owns("/not-this-card/Podcast/Show/Episode.mp3"));
	}
	return NULL;
}

static unsigned delayed_epoch;
static char delayed_path[768];
static void *old_completion(void *unused) {
	(void)unused;
	char snapshot[512];
	assert(podcastdl_root(snapshot, sizeof(snapshot)));
	delayed_epoch = podcaststate_card_epoch();
	snprintf(delayed_path, sizeof(delayed_path), "%s/Show/old.mp3", snapshot);
	pthread_barrier_wait(&rendezvous);
	pthread_barrier_wait(&rendezvous);
	assert(podcastdl_owns(delayed_path)); // Same pathname on the replacement card.
	podcaststate_downloaded(7, delayed_path, delayed_epoch);
	return NULL;
}
int main(void) {
	alarm(30);
	atomic_store(&active_root, "/fixture/card");
	char root[512], saved[512], path[768], tiny[2] = "x";
	assert(podcastdl_root(root, sizeof(root)));
	snprintf(saved, sizeof(saved), "%s", root);
	assert(!podcastdl_root(NULL, sizeof(root)));
	assert(!podcastdl_root(tiny, 0) && tiny[0] == 'x');
	assert(!podcastdl_root(tiny, 1) && !tiny[0]);
	assert(!podcastdl_root(tiny, sizeof(tiny)) && !tiny[0]);
	assert(podcastdl_owns("/FIXTURE/CARD/podcast/Show/Episode.mp3"));
	assert(!podcastdl_owns("/fixture/card/Podcast-other/Episode.mp3"));
	assert(!podcastdl_owns("/fixture/card/Podcast"));
	assert(!podcastdl_owns(NULL));
	podcast_episode_t episode = {.id = 9};
	snprintf(episode.title, sizeof(episode.title), "Part 1: Start");
	assert(podcastdl_path(&episode, " .. Show? . ", path, sizeof(path)));
	assert(!strcmp(path, "/fixture/card/Podcast/Show/Part 1 - Start.mp3"));
	assert(!podcastdl_path(&episode, "Show", tiny, sizeof(tiny)) && !tiny[0]);
	assert(!podcastdl_path(NULL, "Show", tiny, sizeof(tiny)) && !tiny[0]);
	atomic_store(&active_root, NULL);
	assert(!podcastdl_root(root, sizeof(root)) && !root[0]);
	assert(!podcastdl_path(&episode, "Show", path, sizeof(path)) && !path[0]);
	assert(!podcastdl_owns(saved));
	assert(!strcmp(saved, "/fixture/card/Podcast"));
	atomic_store(&active_root, "");
	assert(!podcastdl_root(root, sizeof(root)) && !root[0]);
	char long_root[800];
	memset(long_root, 'a', sizeof(long_root) - 1);
	long_root[0] = '/';
	long_root[sizeof(long_root) - 1] = 0;
	atomic_store(&active_root, long_root);
	assert(!podcastdl_root(root, sizeof(root)) && !root[0]);
	assert(!podcastdl_path(&episode, "Show", path, sizeof(path)) && !path[0]);
	pthread_barrier_init(&rendezvous, NULL, 2);
	pthread_t a, b;
	assert(!pthread_create(&a, NULL, concurrent_paths, "/fixture/card-a"));
	assert(!pthread_create(&b, NULL, concurrent_paths, "/fixture/card-b"));
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	// Epoch refusal is separate from string ownership, including same-path attach.
	char fixture[] = "/tmp/sonix-root-ownership-XXXXXX";
	assert(mkdtemp(fixture));
	atomic_store(&active_root, fixture);
	podcaststate_set_root(fixture);
	assert(!pthread_create(&a, NULL, old_completion, NULL));
	pthread_barrier_wait(&rendezvous);
	podcaststate_set_root(NULL);
	podcaststate_set_root(fixture);
	podcaststate_t record = {.episode_id = 7};
	write_now("e:7", &record, podcaststate_card_epoch());
	pthread_barrier_wait(&rendezvous);
	pthread_join(a, NULL);
	podcaststate_t result;
	assert(podcaststate_get_episode(7, &result) && !result.path[0]);
	podcaststate_downloaded(7, delayed_path, podcaststate_card_epoch());
	assert(podcaststate_get_episode(7, &result) && !strcmp(result.path, delayed_path));
	podcaststate_set_root(NULL);
	char dbfile[640];
	snprintf(dbfile, sizeof(dbfile), "%s/.local/podcast.db", fixture);
	unlink(dbfile);
	snprintf(dbfile, sizeof(dbfile), "%s/.local/podcast.db-journal", fixture);
	unlink(dbfile);
	snprintf(dbfile, sizeof(dbfile), "%s/.local", fixture);
	rmdir(dbfile);
	assert(!rmdir(fixture));
	pthread_barrier_destroy(&rendezvous);
	puts("PASS podcast root/path/owns concurrency, truncation, lifetime and same-path epoch refusal/current admission");
}
