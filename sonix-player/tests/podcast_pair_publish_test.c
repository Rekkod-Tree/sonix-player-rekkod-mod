#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int fail_tags, fail_rollback, crash_cut, operations;
static void cut(void) {
	if (++operations == crash_cut)
		_exit(77);
}
static int fixture_fsync(int fd) {
	int rc = fsync(fd);
	if (!rc)
		cut();
	return rc;
}
static int fixture_renameat(int a, const char *from, int b, const char *to) {
	if (fail_tags && !strcmp(to, "Episode.mp3.tags")) {
		fail_tags = 0;
		errno = EIO;
		return -1;
	}
	if (fail_rollback && strstr(from, ".a.old") && !strcmp(to, "Episode.mp3")) {
		fail_rollback = 0;
		errno = EIO;
		return -1;
	}
	int rc = renameat(a, from, b, to);
	if (!rc)
		cut();
	return rc;
}
#define renameat fixture_renameat
#define fsync fixture_fsync
#include "src/system/streaming/podcastpair.c"
#undef renameat
#undef fsync
#include <assert.h>
#include <sys/wait.h>

static char folder[512];
static bool asked_cancel, cancel_before_commit, commit_cancel_seen;
static int opens, fd_baseline, cancel_on_open;
static int fd_count(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	while (readdir(d))
		n++;
	closedir(d);
	return n;
}
static int open_folder(void *user) {
	(void)user;
	assert(fd_count() == fd_baseline);
	opens++;
	if (cancel_on_open && opens >= cancel_on_open)
		asked_cancel = true;
	int dir = open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cancel_before_commit && dir >= 0) {
		record_t r;
		file_t mark;
		if (read_record(dir, MARKER, &r, &mark) && !r.committed) {
			int a = openat(dir, "Episode.mp3", O_RDONLY | O_CLOEXEC);
			int t = openat(dir, "Episode.mp3.tags", O_RDONLY | O_CLOEXEC);
			char audio[8], tags[8];
			if (a >= 0 && t >= 0 && read(a, audio, 8) == 8 && read(t, tags, 8) == 8 && !memcmp(audio, "NEW-DATA", 8) && !memcmp(tags, "NEW-TAGS", 8)) {
				asked_cancel = commit_cancel_seen = true;
			}
			if (a >= 0)
				close(a);
			if (t >= 0)
				close(t);
		}
	}
	return dir;
}
static bool is_cancelled(void *user) {
	(void)user;
	return asked_cancel;
}
static void save(const char *name, const char *value) {
	char p[800];
	snprintf(p, sizeof(p), "%s/%s", folder, name);
	FILE *f = fopen(p, "wb");
	assert(f);
	assert(fwrite(value, 1, strlen(value), f) == strlen(value));
	assert(!fclose(f));
}
static void check_file(const char *name, const char *value) {
	char p[800], text[128] = {0};
	snprintf(p, sizeof(p), "%s/%s", folder, name);
	FILE *f = fopen(p, "rb");
	assert(f);
	assert(fread(text, 1, sizeof(text) - 1, f) == strlen(value));
	fclose(f);
	assert(!strcmp(text, value));
}
static podcastpair_audio_t audio_digest(void) {
	char p[800];
	snprintf(p, sizeof(p), "%s/.Episode.mp3-1-1.part", folder);
	struct stat st;
	assert(!stat(p, &st));
	podcastpair_audio_t audio = {.dev = st.st_dev, .ino = st.st_ino, .size = 8};
	sha256_ctx sha;
	sha256_init(&sha);
	sha256_update(&sha, "NEW-DATA", 8);
	sha256_final_hex(&sha, audio.sha256);
	return audio;
}
int main(void) {
	alarm(30);
	char tmp[] = "/tmp/sonix-pair-unit-XXXXXX";
	assert(mkdtemp(tmp));
	for (int mode = 0; mode < 5; mode++) {
		snprintf(folder, sizeof(folder), "%s/case%d", tmp, mode);
		assert(!mkdir(folder, 0700));
		save("Episode.mp3", "OLD-DATA");
		save("Episode.mp3.tags", "OLD-TAGS");
		save(".Episode.mp3-1-1.part", "NEW-DATA");
		save(".Episode.mp3.tags-1-2.tmp", "NEW-TAGS");
		fail_tags = mode == 1 || mode == 2 || mode == 3;
		cancel_before_commit = mode == 4;
		commit_cancel_seen = false;
		fail_rollback = mode == 2;
		asked_cancel = mode == 3;
		opens = 0;
		fd_baseline = fd_count();
		podcastpair_io_t io = {.open_dir = open_folder, .cancelled = is_cancelled, .user = &io};
		podcastpair_audio_t a = audio_digest();
		podcastpair_result_t r = podcastpair_publish(&io, ".Episode.mp3-1-1.part", ".Episode.mp3.tags-1-2.tmp", "Episode.mp3", &a);
		assert(fd_count() == fd_baseline && !podcastpair_busy());
		if (mode == 0) {
			assert(r == PODCASTPAIR_OK);
			check_file("Episode.mp3", "NEW-DATA");
			check_file("Episode.mp3.tags", "NEW-TAGS");
		} else if (mode == 2) {
			assert(r == PODCASTPAIR_RECOVERY);
			assert(podcastpair_recover(&io) == PODCASTPAIR_OK);
			check_file("Episode.mp3", "OLD-DATA");
			check_file("Episode.mp3.tags", "OLD-TAGS");
		} else {
			assert(r == PODCASTPAIR_FAILED);
			check_file("Episode.mp3", "OLD-DATA");
			check_file("Episode.mp3.tags", "OLD-TAGS");
		}
		if (mode == 4)
			assert(commit_cancel_seen);
		cancel_before_commit = false;
		printf("PASS pair mode=%d opens=%d result=%d fd=%d\n", mode, opens, r, fd_baseline);
	}

	// A large old file is hashed only in bounded re-admitted chunks, and Cancel
	// during that read leaves the old pair untouched before any record exists.
	for (int mode = 0; mode < 2; mode++) {
		snprintf(folder, sizeof(folder), "%s/large%d", tmp, mode);
		assert(!mkdir(folder, 0700));
		char path[800], expected[SHA256_HEX_LEN], block[8192];
		memset(block, 'X', sizeof(block));
		snprintf(path, sizeof(path), "%s/Episode.mp3", folder);
		FILE *f = fopen(path, "wb");
		assert(f);
		sha256_ctx sha;
		sha256_init(&sha);
		for (int i = 0; i < 128; i++) {
			assert(fwrite(block, 1, sizeof(block), f) == sizeof(block));
			sha256_update(&sha, block, sizeof(block));
		}
		assert(!fclose(f));
		sha256_final_hex(&sha, expected);
		save("Episode.mp3.tags", "OLD-TAGS");
		save(".Episode.mp3-1-1.part", "NEW-DATA");
		save(".Episode.mp3.tags-1-2.tmp", "NEW-TAGS");
		fail_tags = fail_rollback = crash_cut = 0;
		operations = 0;
		opens = 0;
		asked_cancel = false;
		cancel_on_open = mode ? 4 : 0;
		fd_baseline = fd_count();
		podcastpair_io_t io = {.open_dir = open_folder, .cancelled = is_cancelled, .user = &io};
		podcastpair_audio_t a = audio_digest();
		podcastpair_result_t r = podcastpair_publish(&io, ".Episode.mp3-1-1.part", ".Episode.mp3.tags-1-2.tmp", "Episode.mp3", &a);
		int cost = opens;
		assert(fd_count() == fd_baseline && !podcastpair_busy());
		cancel_on_open = 0;
		asked_cancel = false;
		if (!mode) {
			assert(r == PODCASTPAIR_OK && cost >= 50 && cost <= 80);
			check_file("Episode.mp3", "NEW-DATA");
		} else {
			file_t old;
			assert(r == PODCASTPAIR_FAILED && hash_file(&io, "Episode.mp3", &old, true) && old.data.size == 1048576 && !strcmp(old.data.hash, expected));
			check_file("Episode.mp3.tags", "OLD-TAGS");
		}
		printf("PASS large-old-hash cancel=%d bytes=1048576 admissions=%d descriptors=%d\n", mode, cost, fd_baseline);
	}
	// Process-crash cuts across the real record fsync/backup/publish/commit chain.
	for (int point = 1; point <= 20; point++) {
		snprintf(folder, sizeof(folder), "%s/crash%d", tmp, point);
		assert(!mkdir(folder, 0700));
		save("Episode.mp3", "OLD-DATA");
		save("Episode.mp3.tags", "OLD-TAGS");
		save(".Episode.mp3-1-1.part", "NEW-DATA");
		save(".Episode.mp3.tags-1-2.tmp", "NEW-TAGS");
		fail_tags = fail_rollback = 0;
		asked_cancel = false;
		fd_baseline = fd_count();
		podcastpair_io_t io = {.open_dir = open_folder, .cancelled = is_cancelled, .user = &io};
		podcastpair_audio_t a = audio_digest();
		pid_t child = fork();
		assert(child >= 0);
		if (!child) {
			operations = 0;
			crash_cut = point;
			podcastpair_result_t r = podcastpair_publish(&io, ".Episode.mp3-1-1.part", ".Episode.mp3.tags-1-2.tmp", "Episode.mp3", &a);
			_exit(r == PODCASTPAIR_OK ? 0 : 78);
		}
		int status;
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 77));
		assert(podcastpair_recover(&io) == PODCASTPAIR_OK);
		assert(fd_count() == fd_baseline);
		char path[800], text[16] = {0};
		snprintf(path, sizeof(path), "%s/Episode.mp3", folder);
		FILE *f = fopen(path, "rb");
		assert(f && fread(text, 1, 8, f) == 8);
		fclose(f);
		bool old = !memcmp(text, "OLD-DATA", 8);
		assert(old || !memcmp(text, "NEW-DATA", 8));
		check_file("Episode.mp3.tags", old ? "OLD-TAGS" : "NEW-TAGS");
		assert(podcastpair_ready(path));
		printf("PASS process-crash-cut=%d pair=%s descriptors=%d\n", point, old ? "old" : "new", fd_baseline);
	}
	return 0;
}
