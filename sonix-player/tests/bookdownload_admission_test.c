// From sonix-player: gcc -std=gnu11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Werror -I.
// tests/bookdownload_admission_test.c -o /tmp/book-admission && /tmp/book-admission
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool fail_write, fail_sync;
static int fail_directory_sync, directory_failure = EIO;
static ssize_t test_write(int fd, const void *bytes, size_t n) {
	if (fail_write) {
		fail_write = false;
		errno = ENOSPC;
		return -1;
	}
	return write(fd, bytes, n);
}
static int test_sync(int fd) {
	struct stat st;
	if (fail_directory_sync && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && !--fail_directory_sync) {
		errno = directory_failure;
		return -1;
	}
	if (fail_sync) {
		fail_sync = false;
		errno = EIO;
		return -1;
	}
	return fsync(fd);
}
#define write test_write
#define fsync test_sync
#include "src/system/streaming/bookdownload.c"
#undef write
#undef fsync

typedef struct {
	char root[512];
	unsigned current, expected;
} admission_t;
static admission_t admission;
static unsigned cleanup_admissions;
static bool replace_current;
static char victim[700];
static bool cancelled_job, arm_commit_cancel;
static char cancel_manifest[700];
static bool job_cancelled(void *user) {
	(void)user;
	return cancelled_job;
}
static int open_root(void *user) {
	admission_t *a = user;
	if (arm_commit_cancel && access(cancel_manifest, F_OK) == 0)
		cancelled_job = true;
	if (replace_current && ++cleanup_admissions == 2) {
		char saved[800];
		snprintf(saved, sizeof(saved), "%s.saved", victim);
		assert(!rename(victim, saved));
		int fd = open(victim, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		assert(fd >= 0);
		assert(write(fd, "FOREIGN", 7) == 7);
		assert(!close(fd));
	}
	if (a->current != a->expected) {
		errno = ESTALE;
		return -1;
	}
	return open(a->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
static int fds(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	while (readdir(d))
		n++;
	closedir(d);
	return n;
}
static void save_at(int dir, const char *name, const char *text) {
	int fd = openat(dir, name, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
	assert(fd >= 0);
	assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	assert(!close(fd));
}
static void pair(bookdownload_t *book, const char *name) {
	int dir = bookdownload_open_stage(book);
	assert(dir >= 0);
	save_at(dir, name, "COMPLETE-AUDIO");
	char tags[96];
	snprintf(tags, sizeof(tags), "%s.tags", name);
	save_at(dir, tags, "title=Chapter\n");
	assert(!close(dir));
	assert(bookdownload_record(book, name));
}
static void setup(const char *base, const char *leaf, char directory[512]) {
	snprintf(admission.root, sizeof(admission.root), "%s/%s", base, leaf);
	assert(!mkdir(admission.root, 0700));
	admission.expected = ++admission.current;
	cancelled_job = arm_commit_cancel = false;
	assert(bookdownload_path(admission.root, "Books", "yandex", "123", directory, 512));
}
int main(void) {
	alarm(30);
	char tmp[] = "/tmp/sonix-book-admission-XXXXXX";
	assert(mkdtemp(tmp));
	bookdownload_t *book = calloc(1, sizeof(*book));
	assert(book);
	bookdownload_io_t io = {.open_root = open_root, .cancelled = job_cancelled, .user = &admission};
	char directory[512];
	int before = fds();
	setup(tmp, "normal", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	pair(book, "0001-1.mp3");
	assert(bookdownload_validate(book) && fds() == before);
	int dir = bookdownload_open_stage(book);
	assert(dir >= 0);
	const char *partials[] = {"0002-2.mp3", "0002-2.mp3.tags", ".0002-2.mp3.part", ".0002-2.mp3.part.tags", "0002-2.mp3.part", "0002-2.mp3.tags.part", "0002-2.mp3.tags.tmp"};
	for (size_t i = 0; i < sizeof(partials) / sizeof(partials[0]); i++)
		save_at(dir, partials[i], "PARTIAL");
	close(dir);
	assert(!bookdownload_discard(NULL, "0002-2.mp3"));
	assert(bookdownload_discard(book, "0002-2.mp3"));
	dir = bookdownload_open_stage(book);
	assert(dir >= 0);
	for (size_t i = 0; i < sizeof(partials) / sizeof(partials[0]); i++) {
		struct stat st;
		assert(fstatat(dir, partials[i], &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
	}
	close(dir);
	assert(!bookdownload_discard(book, "0001-1.mp3"));
	pair(book, "0002-2.mp3");
	assert(bookdownload_publish(book, 2) && bookdownload_ready(directory, NULL, 0) && fds() == before);
	puts("PASS completed chapters survive closed-FD retry, current partial only discarded, whole book published");
	setup(tmp, "epoch", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	pair(book, "0001-1.mp3");
	admission.current++;
	assert(!bookdownload_validate(book) && bookdownload_open_stage(book) < 0 && !bookdownload_publish(book, 1));
	bookdownload_abort(book);
	assert(book->owned && fds() == before);
	puts("PASS changed card epoch refuses every stage operation even with unchanged real dev/inode");
	setup(tmp, "parent", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	pair(book, "0001-1.mp3");
	char parent[700], old[700];
	snprintf(parent, sizeof(parent), "%s/Books", admission.root);
	snprintf(old, sizeof(old), "%s/Books-old", admission.root);
	assert(!rename(parent, old) && !mkdir(parent, 0700));
	assert(!bookdownload_validate(book) && bookdownload_open_stage(book) < 0 && !bookdownload_publish(book, 1));
	bookdownload_abort(book);
	assert(book->owned && fds() == before);
	puts("PASS same-card parent replacement cannot redirect old book transaction");
	setup(tmp, "stage", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	pair(book, "0001-1.mp3");
	snprintf(old, sizeof(old), "%s-old", book->folder);
	assert(!rename(book->folder, old) && !mkdir(book->folder, 0700));
	dir = open(book->folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	assert(dir >= 0);
	save_at(dir, OWNER, FORMAT);
	save_at(dir, "0001-1.mp3", "FOREIGN");
	close(dir);
	assert(!bookdownload_validate(book) && !bookdownload_record(book, "0002-2.mp3"));
	bookdownload_abort(book);
	assert(book->owned && fds() == before);
	puts("PASS same-card stage replacement preserves foreign folder");
	setup(tmp, "commit-cancel", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	pair(book, "0001-1.mp3");
	snprintf(cancel_manifest, sizeof(cancel_manifest), "%s/%s", book->folder, MANIFEST);
	arm_commit_cancel = true;
	assert(!bookdownload_publish(book, 1) && cancelled_job && access(directory, F_OK) != 0);
	arm_commit_cancel = false;
	bookdownload_abort(book);
	assert(!book->owned && fds() == before);
	puts("PASS Cancel during final parent admission blocks publication while owned cleanup remains usable");
	for (int mode = 0; mode < 4; mode++) {
		char leaf[32];
		snprintf(leaf, sizeof(leaf), "directory-sync%d", mode);
		setup(tmp, leaf, directory);
		assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
		pair(book, "0001-1.mp3");
		fail_directory_sync = mode == 1 ? 2 : 1;
		directory_failure = mode == 2 ? EINVAL : mode == 3 ? ENOSPC : EIO;
		bool published = bookdownload_publish(book, 1);
		assert(published == (mode == 2));
		if (mode != 2)
			assert(errno == directory_failure);
		bool public_ready = bookdownload_ready(directory, NULL, 0);
		assert(public_ready == (mode == 1 || mode == 2));
		assert(book->owned == (mode == 0 || mode == 3));
		bookdownload_abort(book);
		assert(!book->owned && fds() == before);
		if (mode == 1 || mode == 2)
			assert(bookdownload_ready(directory, NULL, 0));
	}
	puts("PASS directory EIO/ENOSPC propagates, unsupported sync tolerated, published-but-sync-failed book never aborted");
	setup(tmp, "checkpoint", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	dir = bookdownload_open_stage(book);
	assert(dir >= 0);
	save_at(dir, "0001-1.mp3", "AUDIO");
	save_at(dir, "0001-1.mp3.tags", "TAGS");
	close(dir);
	fail_sync = true;
	assert(!bookdownload_record(book, "0001-1.mp3") && book->count == 0 && errno == EIO);
	assert(bookdownload_record(book, "0001-1.mp3"));
	bookdownload_abort(book);
	assert(!book->owned && fds() == before);
	puts("PASS delayed chapter fsync error does not confirm a checkpoint");
	setup(tmp, "current-replacement", directory);
	assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
	dir = bookdownload_open_stage(book);
	assert(dir >= 0);
	save_at(dir, "0002-2.mp3.part", "OWNED");
	close(dir);
	snprintf(victim, sizeof(victim), "%s/0002-2.mp3.part", book->folder);
	cleanup_admissions = 0;
	replace_current = true;
	assert(!bookdownload_discard(book, "0002-2.mp3"));
	replace_current = false;
	assert(book->conflicted);
	bookdownload_abort(book);
	FILE *foreign = fopen(victim, "rb");
	assert(foreign);
	char text[8] = {0};
	assert(fread(text, 1, 7, foreign) == 7);
	assert(!fclose(foreign) && !strcmp(text, "FOREIGN") && book->owned && fds() == before);
	puts("PASS replaced current partial survives discard readmission and terminal abort");
	for (int mode = 0; mode < 2; mode++) {
		char leaf[32];
		snprintf(leaf, sizeof(leaf), "owner-fault%d", mode);
		setup(tmp, leaf, directory);
		fail_write = mode == 0;
		fail_sync = mode == 1;
		assert(!bookdownload_begin_admitted(book, admission.root, directory, &io));
		assert(fds() == before);
		assert(bookdownload_begin_admitted(book, admission.root, directory, &io));
		bookdownload_abort(book);
		assert(!book->owned && fds() == before);
	}
	puts("PASS owner ENOSPC/EIO leaves no unowned permanent retry blocker");
	free(book);
	return 0;
}
