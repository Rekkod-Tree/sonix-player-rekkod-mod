#ifndef BOOKDOWNLOAD_H
#define BOOKDOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>

#define BOOKDOWNLOAD_MAX_PARTS 300
#define BOOKDOWNLOAD_PATH_MAX 512

// Flat private staging folder, published in one rename. The caller holds a
// storage pin throughout begin/write/record/publish/abort. No network here.
typedef struct {
	char folder[512];
	char final[512];
	char files[BOOKDOWNLOAD_MAX_PARTS][80];
	int count;
	bool owned;
} bookdownload_t;

bool bookdownload_path(const char *root, const char *folder, const char *service, const char *id, char *out, size_t size);
// True only for a complete publication whose audio and metadata files exist.
// Returns the scanner's book key: a file for one part, otherwise its folder.
bool bookdownload_ready(const char *directory, char *book_out, size_t size);
bool bookdownload_begin(bookdownload_t *book, const char *directory);
bool bookdownload_record(bookdownload_t *book, const char *filename);
bool bookdownload_publish(bookdownload_t *book, int expected);
void bookdownload_abort(bookdownload_t *book);

#endif
