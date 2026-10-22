// From sonix-player: gcc -std=gnu11 -Wall -Wextra -Werror -I.
// tests/podcast_pair_readiness_barrier_test.c src/system/core/sha256.c -pthread -o /tmp/pair-ready
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool armed, begin_publish, partial, resume_publish;
static int hook_stat(int dir, const char *path, struct stat *st, int flags) {
	int rc = fstatat(dir, path, st, flags), saved = errno;
	if (armed && !strcmp(path, ".sonix-pair.txn") && rc < 0 && saved == ENOENT) {
		pthread_mutex_lock(&lock);
		armed = false;
		begin_publish = true;
		pthread_cond_broadcast(&changed);
		while (!partial)
			pthread_cond_wait(&changed, &lock);
		pthread_mutex_unlock(&lock);
	}
	errno = saved;
	return rc;
}
static int hook_rename(int a, const char *from, int b, const char *to) {
	int rc = renameat(a, from, b, to);
	if (!rc && !strcmp(from, ".Episode.mp3-1-1.part") && !strcmp(to, "Episode.mp3")) {
		pthread_mutex_lock(&lock);
		partial = true;
		pthread_cond_broadcast(&changed);
		while (!resume_publish)
			pthread_cond_wait(&changed, &lock);
		pthread_mutex_unlock(&lock);
	}
	return rc;
}
#define fstatat hook_stat
#define renameat hook_rename
#include "src/system/streaming/podcastpair.c"
#undef fstatat
#undef renameat

static char folder[512];
static int open_folder(void *user) {
	(void)user;
	return open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
static void save(int dir, const char *name, const char *bytes, size_t size) {
	int fd = openat(dir, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	assert(fd >= 0 && write(fd, bytes, size) == (ssize_t)size && !close(fd));
}
static void *publisher(void *unused) {
	(void)unused;
	pthread_mutex_lock(&lock);
	while (!begin_publish)
		pthread_cond_wait(&changed, &lock);
	pthread_mutex_unlock(&lock);
	int dir = open_folder(NULL);
	struct stat st;
	assert(!fstatat(dir, ".Episode.mp3-1-1.part", &st, 0));
	close(dir);
	podcastpair_audio_t audio = {.dev = st.st_dev, .ino = st.st_ino, .size = 8};
	sha256_ctx sha;
	sha256_init(&sha);
	sha256_update(&sha, "NEW-DATA", 8);
	sha256_final_hex(&sha, audio.sha256);
	podcastpair_io_t io = {.open_dir = open_folder, .user = &audio};
	assert(podcastpair_publish(&io, ".Episode.mp3-1-1.part", ".Episode.mp3.tags-1-2.tmp", "Episode.mp3", &audio) == PODCASTPAIR_OK);
	return NULL;
}
int main(void) {
	alarm(15);
	char tmp[] = "/tmp/sonix-independent-ready-XXXXXX";
	assert(mkdtemp(tmp));
	snprintf(folder, sizeof(folder), "%s", tmp);
	int dir = open_folder(NULL);
	save(dir, "Episode.mp3", "OLD-DATA", 8);
	save(dir, "Episode.mp3.tags", "OLD-TAGS", 8);
	save(dir, ".Episode.mp3-1-1.part", "NEW-DATA", 8);
	save(dir, ".Episode.mp3.tags-1-2.tmp", "NEW-TAGS", 8);
	save(dir, "Other.mp3", "OTHER", 5);
	pthread_t worker;
	assert(!pthread_create(&worker, NULL, publisher, NULL));
	armed = true;
	bool ready = podcastpair_ready_at(dir, "Episode.mp3");
	struct stat st;
	bool marker = !fstatat(dir, MARKER, &st, AT_SYMLINK_NOFOLLOW);
	bool tags = !fstatat(dir, "Episode.mp3.tags", &st, AT_SYMLINK_NOFOLLOW);
	printf("MARKERLESS_RACE ready=%d marker_present=%d tags_present=%d expected_ready=0\n", ready, marker, tags);
	char record[4096];
	int fd = openat(dir, MARKER, O_RDONLY);
	assert(fd >= 0);
	ssize_t length = read(fd, record, sizeof(record));
	assert(length > 0);
	close(fd);
	pthread_mutex_lock(&lock);
	resume_publish = true;
	pthread_cond_broadcast(&changed);
	pthread_mutex_unlock(&lock);
	assert(!pthread_join(worker, NULL));
	save(dir, MARKER, record, (size_t)length);
	save(dir, MARKER ".next", "UNKNOWN", 7);
	bool unrelated = podcastpair_ready_at(dir, "Other.mp3");
	printf("UNKNOWN_NEXT unrelated_ready=%d expected_ready=0\n", unrelated);
	DIR *entries = fdopendir(dup(dir));
	struct dirent *entry;
	while ((entry = readdir(entries)))
		if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
			assert(!unlinkat(dir, entry->d_name, 0));
	closedir(entries);
	close(dir);
	assert(!rmdir(folder));
	return ready || unrelated ? 1 : 0;
}
