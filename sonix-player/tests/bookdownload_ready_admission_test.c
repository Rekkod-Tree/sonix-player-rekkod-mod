// gcc -std=gnu11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Werror -I. tests/bookdownload_ready_admission_test.c src/system/streaming/bookdownload.c -o /tmp/book-ready-admission
#define _GNU_SOURCE
#include "src/system/streaming/bookdownload.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
	char root[512], directory[512];
	unsigned expected, current, calls;
	int mode;
	bool cancelled;
} fixture_t;
static void write_file(int dir, const char *name, const char *text) {
	int fd = openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	assert(fd >= 0);
	assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	assert(!close(fd));
}
static void published(const char *directory, int parts, const char *audio) {
	bookdownload_t *book = calloc(1, sizeof(*book));
	assert(book && bookdownload_begin(book, directory));
	for (int i = 1; i <= parts; i++) {
		char name[80], tags[96];
		snprintf(name, sizeof(name), "%04d-%d.mp3", i, i);
		snprintf(tags, sizeof(tags), "%s.tags", name);
		int fd = bookdownload_open_stage(book);
		assert(fd >= 0);
		write_file(fd, name, audio);
		write_file(fd, tags, "title=fixture\n");
		close(fd);
		assert(bookdownload_record(book, name));
	}
	assert(bookdownload_publish(book, parts));
	free(book);
}
static int root_open(void *user) {
	fixture_t *f = user;
	f->calls++;
	if (f->calls == 2) {
		char old[700];
		if (f->mode == 1)
			f->current++;
		else if (f->mode == 2) {
			snprintf(old, sizeof(old), "%s-old", f->root);
			assert(!rename(f->root, old) && !mkdir(f->root, 0700));
			published(f->directory, 1, "B-AUDIO");
		} else if (f->mode == 3) {
			snprintf(old, sizeof(old), "%s-old", f->directory);
			assert(!rename(f->directory, old));
			published(f->directory, 1, "B-AUDIO");
		} else if (f->mode == 4)
			f->cancelled = true;
	}
	if (f->expected != f->current) {
		errno = ESTALE;
		return -1;
	}
	return open(f->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
static bool cancelled(void *user) { return ((fixture_t *)user)->cancelled; }
static int fds(void) {
	DIR *d = opendir("/proc/self/fd");
	assert(d);
	int n = 0;
	while (readdir(d))
		n++;
	closedir(d);
	return n;
}
int main(void) {
	alarm(30);
	char tmp[] = "/tmp/sonix-book-ready-admission-XXXXXX";
	assert(mkdtemp(tmp));
	int baseline = fds();
	for (int mode = 0; mode < 6; mode++) {
		fixture_t f = {.expected = 1, .current = 1, .mode = mode};
		snprintf(f.root, sizeof(f.root), "%s/case%d", tmp, mode);
		assert(!mkdir(f.root, 0700));
		assert(bookdownload_path(f.root, "Books", "yandex", "123", f.directory, sizeof(f.directory)));
		int parts = mode == 5 ? 2 : 1;
		published(f.directory, parts, "A-AUDIO");
		bookdownload_io_t io = {.open_root = root_open, .cancelled = cancelled, .user = &f};
		char key[700] = "MUST-CLEAR";
		bool ready = bookdownload_ready_admitted(f.root, f.directory, &io, key, sizeof(key));
		assert(ready == (mode == 0 || mode == 5));
		if (ready) {
			char expected[700];
			if (parts == 1)
				snprintf(expected, sizeof(expected), "%s/0001-1.mp3", f.directory);
			else
				snprintf(expected, sizeof(expected), "%s", f.directory);
			assert(!strcmp(key, expected) && !strstr(key, "/proc/"));
		} else
			assert(!key[0]);
		assert(bookdownload_ready(f.directory, NULL, 0));
		assert(fds() == baseline);
		printf("PASS admitted-ready mode=%d canonical=%d callbacks=%u descriptors=%d\n", mode, ready, f.calls, baseline);
	}
	puts("PASS old epoch with same dev/inode, replacement root/folder, late Cancel never claim another complete book");
	return 0;
}
