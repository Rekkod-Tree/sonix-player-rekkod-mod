#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool allocation_fails, sync_fails;
static unsigned flushes;
static int injected_close(int fd);
static int injected_unlinkat(int dir, const char *name, int flags);
static void *allocate_copy(size_t n) {
	if (allocation_fails) {
		errno = ENOMEM;
		return NULL;
	}
	return malloc(n);
}
static int flush_filesystem(int fd) {
	struct stat st;
	assert(!fstat(fd, &st));
	flushes++;
	if (sync_fails) {
		errno = EIO;
		return -1;
	}
	return 0;
}
#define malloc allocate_copy
#define syncfs flush_filesystem
#define close injected_close
#define unlinkat injected_unlinkat
#include "src/system/device/fileops.c"
#undef malloc
#undef syncfs
#undef close
#undef unlinkat

static uint64_t active_epoch = 1;
static char card[FILEOP_ROOT_MAX], second[FILEOP_ROOT_MAX], parked[FILEOP_ROOT_MAX];
static bool busy_owner, cancel_now, cutover, became_stale, busy_after_chunk;
static bool before_copy, between_files, before_delete;
static uint64_t progressed;

static void replace_card(void) {
	assert(!rename(card, parked));
	assert(!rename(second, card));
	active_epoch++;
	became_stale = true;
}

static int injected_close(int fd) {
	bool replace = false;
	if (between_files && !became_stale) {
		char node[64], path[FILEOP_PATH_MAX], prefix[FILEOP_PATH_MAX];
		snprintf(node, sizeof(node), "/proc/self/fd/%d", fd);
		ssize_t n = readlink(node, path, sizeof(path) - 1);
		if (n > 0) {
			path[n] = 0;
			snprintf(prefix, sizeof(prefix), "%s/dest/", card);
			replace = !strncmp(path, prefix, strlen(prefix));
		}
	}
	int result = close(fd);
	if (!result && replace)
		replace_card(); // Complete file closed, next one unopened.
	return result;
}

static int injected_unlinkat(int dir, const char *name, int flags) {
	if (before_delete && !became_stale)
		replace_card();
	// Revocation after the last owner check: this syscall must still address
	// the pinned original parent, never the same path on the new card.
	return unlinkat(dir, name, flags);
}

int storage_pin_card_if(const storage_card_token_t *wanted) {
	if (wanted->epoch != active_epoch) {
		errno = ESTALE;
		return -1;
	}
	return open(card, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
int storage_card_try_check(const storage_card_token_t *wanted) {
	if (busy_owner) {
		errno = EAGAIN;
		return -1;
	}
	if (wanted->epoch != active_epoch) {
		errno = ESTALE;
		return -1;
	}
	return 0;
}
static bool cancelled(void *unused) {
	(void)unused;
	return cancel_now;
}
static void progress(uint64_t done, uint64_t total, void *unused) {
	(void)unused;
	assert(done <= total);
	progressed = done;
	if ((done || before_copy) && !became_stale && cutover)
		replace_card();
	if (done && busy_after_chunk)
		busy_owner = true;
}
static void path_join(char *out, size_t size, const char *dir, const char *name) {
	int n = snprintf(out, size, "%s/%s", dir, name);
	assert(n > 0 && (size_t)n < size);
}
static void make_file(const char *root, const char *name, const char *value) {
	char path[FILEOP_PATH_MAX];
	path_join(path, sizeof(path), root, name);
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	assert(fd >= 0);
	size_t n = strlen(value);
	assert(write(fd, value, n) == (ssize_t)n);
	assert(!close(fd));
}
static bool exists(const char *root, const char *name) {
	char path[FILEOP_PATH_MAX];
	path_join(path, sizeof(path), root, name);
	struct stat st;
	return !lstat(path, &st);
}
static void content(const char *root, const char *name, const char *expected) {
	char path[FILEOP_PATH_MAX], bytes[64] = {0};
	path_join(path, sizeof(path), root, name);
	int fd = open(path, O_RDONLY | O_NOFOLLOW);
	assert(fd >= 0);
	ssize_t n = read(fd, bytes, sizeof(bytes) - 1);
	assert(n >= 0);
	close(fd);
	assert(!strcmp(bytes, expected));
}
static fileop_request_t request(fileop_kind_t kind, const char *from, const char *to) {
	fileop_request_t r = {.kind = kind, .card = {.epoch = active_epoch}};
	snprintf(r.root, sizeof(r.root), "%s", card);
	if (from)
		path_join(r.source, sizeof(r.source), card, from);
	if (to)
		path_join(r.target, sizeof(r.target), card, to);
	return r;
}
static int descriptors(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)))
		if (ordinary_name(e->d_name))
			n++;
	closedir(d);
	return n;
}
static int execute(fileop_request_t *r) {
	int before = descriptors();
	int result = fileop_run(r, progress, cancelled, NULL);
	assert(descriptors() == before);
	return result;
}
static void basic(void) {
	char sub[FILEOP_PATH_MAX];
	path_join(sub, sizeof(sub), card, "tree");
	assert(!mkdir(sub, 0700));
	make_file(sub, "music", "music");
	make_file(sub, ".hidden", "hidden");
	fileop_request_t r = request(FILEOP_COPY, "tree", "copy");
	assert(!execute(&r));
	char copy[FILEOP_PATH_MAX];
	path_join(copy, sizeof(copy), card, "copy");
	content(copy, ".hidden", "hidden");
	content(copy, "music", "music");
	assert(execute(&r) == EEXIST);
	content(copy, "music", "music");
	r = request(FILEOP_MOVE, "copy", "moved");
	assert(!execute(&r));
	assert(!exists(card, "copy") && exists(card, "moved"));
	r = request(FILEOP_RENAME, "moved", "tree");
	assert(execute(&r) == EEXIST);
	assert(exists(card, "moved"));
	r = request(FILEOP_MKDIR, NULL, "empty");
	assert(!execute(&r));
	r = request(FILEOP_DELETE, "moved", NULL);
	assert(!execute(&r));
	assert(!exists(card, "moved"));
	r = request(FILEOP_DELETE, "empty", NULL);
	assert(!execute(&r));
	r = request(FILEOP_COPY, "tree", "tree/child");
	assert(execute(&r) == EINVAL);
	assert(!exists(sub, "child"));
}
static void bounds(void) {
	make_file(card, "source", "original");
	fileop_request_t r = request(FILEOP_COPY, "../outside", "dest");
	assert(execute(&r) == EINVAL);
	r = request(FILEOP_COPY, "./source", "dest");
	assert(execute(&r) == EINVAL);
	r = request(FILEOP_DELETE, NULL, NULL);
	snprintf(r.source, sizeof(r.source), "%s", card);
	assert(execute(&r) == EINVAL);
	r = request(FILEOP_COPY, "source", "dest");
	snprintf(r.target, sizeof(r.target), "%s-other/dest", card);
	assert(execute(&r) == EINVAL);
	char link[FILEOP_PATH_MAX], outside[FILEOP_PATH_MAX];
	path_join(link, sizeof(link), card, "link");
	path_join(outside, sizeof(outside), second, "secret");
	make_file(second, "secret", "untouched");
	assert(!symlink(second, link));
	r = request(FILEOP_COPY, "link/secret", "escaped");
	assert(execute(&r) != 0);
	assert(!exists(card, "escaped"));
	content(second, "secret", "untouched");
	// Copying the link itself keeps a link, and deleting it never follows it.
	r = request(FILEOP_COPY, "link", "link-copy");
	assert(!execute(&r));
	char dest[FILEOP_PATH_MAX], got[FILEOP_PATH_MAX];
	path_join(dest, sizeof(dest), card, "link-copy");
	ssize_t n = readlink(dest, got, sizeof(got) - 1);
	assert(n > 0);
	got[n] = 0;
	assert(!strcmp(got, second));
	r = request(FILEOP_DELETE, "link-copy", NULL);
	assert(!execute(&r));
	content(second, "secret", "untouched");
	path_join(dest, sizeof(dest), card, "pipe");
	assert(!mkfifo(dest, 0600));
	r = request(FILEOP_COPY, "pipe", "pipe-copy");
	assert(execute(&r) == ENOTSUP);
}
static void large(void) {
	char path[FILEOP_PATH_MAX];
	path_join(path, sizeof(path), card, "source");
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	assert(fd >= 0);
	char bytes[COPY_CHUNK];
	memset(bytes, 'x', sizeof(bytes));
	for (int i = 0; i < 4; i++)
		assert(write(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
	close(fd);
}
static void card_case(void) {
	large();
	make_file(second, "dest", "new-card-sentinel");
	fileop_request_t r = request(FILEOP_COPY, "source", "dest");
	cutover = true;
	assert(execute(&r) == ESTALE && became_stale && progressed > 0);
	content(card, "dest", "new-card-sentinel");
	assert(!exists(parked, "dest"));
	assert(exists(parked, "source"));
	// A queued request cannot adopt a new card even when the absolute path is
	// identical to its old mount name.
	assert(execute(&r) == ESTALE);
	content(card, "dest", "new-card-sentinel");
	r.kind = FILEOP_DELETE;
	assert(execute(&r) == ESTALE);
	content(card, "dest", "new-card-sentinel");
}
static void failure_case(void) {
	large();
	fileop_request_t r = request(FILEOP_COPY, "source", "dest");
	allocation_fails = true;
	assert(execute(&r) == ENOMEM);
	assert(!exists(card, "dest"));
	allocation_fails = false;
	cancel_now = true;
	assert(execute(&r) == ECANCELED);
	assert(!exists(card, "dest"));
	cancel_now = false;
	busy_after_chunk = true;
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	assert(execute(&r) == EAGAIN);
	clock_gettime(CLOCK_MONOTONIC, &end);
	assert((end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000 < 1000);
	assert(!exists(card, "dest"));
	busy_owner = busy_after_chunk = false;
	sync_fails = true;
	assert(execute(&r) == EIO);
	assert(exists(card, "dest") && exists(card, "source"));
}
static void boundary_case(const char *which) {
	char source[FILEOP_PATH_MAX], destination[FILEOP_PATH_MAX];
	path_join(source, sizeof(source), card, "source");
	assert(!mkdir(source, 0700));
	make_file(source, "a", "a");
	make_file(source, "b", "b");
	bool deleting = !strcmp(which, "before-delete");
	path_join(destination, sizeof(destination), second, deleting ? "source" : "dest");
	assert(!mkdir(destination, 0700));
	make_file(destination, "a", "new-card-a");
	make_file(destination, "b", "new-card-b");
	fileop_request_t r = request(deleting ? FILEOP_DELETE : FILEOP_COPY, "source", deleting ? NULL : "dest");
	if (!strcmp(which, "before-copy"))
		cutover = before_copy = true;
	else if (!strcmp(which, "between-files"))
		between_files = true;
	else
		before_delete = true;
	assert(execute(&r) == ESTALE && became_stale);
	path_join(destination, sizeof(destination), card, deleting ? "source" : "dest");
	content(destination, "a", "new-card-a");
	content(destination, "b", "new-card-b");
	if (!deleting) {
		path_join(source, sizeof(source), parked, "source");
		content(source, "a", "a");
		content(source, "b", "b");
	}
	if (before_copy)
		assert(!exists(parked, "dest"));
	if (between_files) {
		path_join(destination, sizeof(destination), parked, "dest");
		assert((int)exists(destination, "a") + (int)exists(destination, "b") == 1);
	}
}
int main(int argc, char **argv) {
	assert(argc == 3);
	path_join(card, sizeof(card), argv[1], "card");
	path_join(second, sizeof(second), argv[1], "second");
	path_join(parked, sizeof(parked), argv[1], "parked");
	assert(!mkdir(card, 0700) && !mkdir(second, 0700));
	if (!strcmp(argv[2], "basic"))
		basic();
	else if (!strcmp(argv[2], "bounds"))
		bounds();
	else if (!strcmp(argv[2], "card"))
		card_case();
	else if (!strcmp(argv[2], "failures"))
		failure_case();
	else if (!strcmp(argv[2], "before-copy") || !strcmp(argv[2], "between-files") || !strcmp(argv[2], "before-delete"))
		boundary_case(argv[2]);
	else
		assert(false);
	puts("PASS card-bound real filesystem operations, refusal paths and descriptor release");
	return 0;
}
