/* cc -std=gnu11 -O1 -Wall -Wextra -I. tests/bookdownload_test.c
 * src/system/streaming/bookdownload.c -o /tmp/book-publication && /tmp/book-publication
 * Normal text stand-ins test publication/ownership, not an audio decoder.
 */
#define _GNU_SOURCE
#include "src/system/streaming/bookdownload.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void file(const char *dir, const char *name, const char *text) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = fopen(path, "w");
	assert(f);
	assert(fputs(text, f) >= 0 && fclose(f) == 0);
}
static void part(bookdownload_t *b, const char *name) {
	file(b->folder, name, "audio fixture\n");
	char tags[96];
	snprintf(tags, sizeof(tags), "%s.tags", name);
	file(b->folder, tags, "title=Chapter\nalbum=Book\n");
	assert(bookdownload_record(b, name));
}
int main(void) {
	char root[] = "/tmp/book-publish-XXXXXX", parent[512], directory[512], key[512];
	assert(mkdtemp(root));
	snprintf(parent, sizeof(parent), "%s/Books", root);
	assert(mkdir(parent, 0700) == 0);
	assert(bookdownload_path(root, "Books", "yandex", "38681851", directory, sizeof(directory)));
	assert(!bookdownload_path(root, "../Music", "yandex", "38681851", key, sizeof(key)));
	bookdownload_t *b = calloc(1, sizeof(*b));
	assert(b);
	assert(bookdownload_begin(b, directory));
	part(b, "0001-10.mp3");
	assert(!bookdownload_publish(b, 2) && !bookdownload_ready(directory, key, sizeof(key)));
	// Restart after an incomplete job removes its private stage, not Music or final books.
	file(parent, "keep.txt", "unrelated\n");
	assert(bookdownload_begin(b, directory));
	part(b, "0001-10.mp3");
	part(b, "0002-11.mp3");
	assert(bookdownload_publish(b, 2));
	assert(bookdownload_ready(directory, key, sizeof(key)) && strcmp(key, directory) == 0);
	struct stat before, after;
	assert(stat(directory, &before) == 0);
	assert(!bookdownload_begin(b, directory));
	assert(stat(directory, &after) == 0 && before.st_ino == after.st_ino);
	assert(bookdownload_ready(directory, key, sizeof(key)));
	// A missing metadata file makes an incomplete publication explicitly fail.
	char path[600];
	snprintf(path, sizeof(path), "%s/0002-11.mp3.tags", directory);
	assert(unlink(path) == 0);
	assert(!bookdownload_ready(directory, key, sizeof(key)));
	const char *names[] = {"0001-10.mp3", "0001-10.mp3.tags", "0002-11.mp3", ".sonix-book", ".sonix-download"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
		assert(unlink(path) == 0);
	}
	assert(rmdir(directory) == 0);
	assert(bookdownload_begin(b, directory));
	part(b, "0001-10.mp3");
	assert(bookdownload_publish(b, 1));
	assert(bookdownload_ready(directory, key, sizeof(key)) && strstr(key, "/0001-10.mp3"));
	const char *one[] = {"0001-10.mp3", "0001-10.mp3.tags", ".sonix-book", ".sonix-download"};
	for (size_t i = 0; i < sizeof(one) / sizeof(one[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", directory, one[i]);
		assert(unlink(path) == 0);
	}
	assert(rmdir(directory) == 0);
	snprintf(path, sizeof(path), "%s/keep.txt", parent);
	assert(unlink(path) == 0);
	assert(rmdir(parent) == 0 && rmdir(root) == 0);
	free(b);
	puts("Book complete publication, retry and stable identity: PASS");
}
