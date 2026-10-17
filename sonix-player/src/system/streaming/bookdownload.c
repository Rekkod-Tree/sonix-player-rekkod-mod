#include "bookdownload.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OWNER ".sonix-download"
#define MANIFEST ".sonix-book"
#define FORMAT "SONIX-BOOK-1\n"

static bool join(char *out, size_t size, const char *dir, const char *name) {
	int n = snprintf(out, size, "%s/%s", dir, name);
	return n > 0 && (size_t)n < size;
}

static bool identifier(const char *s) {
	if (!s || !s[0] || strlen(s) > 40)
		return false;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '-')
			return false;
	return true;
}

bool bookdownload_path(const char *root, const char *folder, const char *service, const char *id, char *out, size_t size) {
	if (!root || !root[0] || !folder || !folder[0] || !identifier(service) || !identifier(id))
		return false;
	for (const char *p = folder; *p;) {
		size_t n = strcspn(p, "/");
		if (!n || *p == '.' || memchr(p, '\\', n))
			return false;
		p += n;
		if (*p && !*++p)
			return false;
	}
	int n = snprintf(out, size, "%s/%s/%s-%s", root, folder, service, id);
	// Leave room for staging prefix and chapter filename plus tags suffix.
	return n > 0 && (size_t)n < size && n + 100 < BOOKDOWNLOAD_PATH_MAX;
}

static bool regular_size(const char *dir, const char *name, long long *size) {
	char path[512];
	struct stat st;
	if (!join(path, sizeof(path), dir, name) || lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
		return false;
	*size = (long long)st.st_size;
	return true;
}

static bool filename_ok(const char *name) {
	if (!name || !name[0] || strlen(name) >= 80 || name[0] == '.')
		return false;
	for (const char *p = name; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '-' && *p != '.')
			return false;
	return true;
}

static bool part_sizes(const char *dir, const char *name, long long *audio, long long *tags) {
	char sidecar[96];
	snprintf(sidecar, sizeof(sidecar), "%s.tags", name);
	return filename_ok(name) && regular_size(dir, name, audio) && regular_size(dir, sidecar, tags);
}

bool bookdownload_ready(const char *directory, char *book_out, size_t size) {
	char path[512], line[160], first[80] = "";
	struct stat st;
	if (lstat(directory, &st) != 0 || !S_ISDIR(st.st_mode) || !join(path, sizeof(path), directory, MANIFEST))
		return false;
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(line, sizeof(line), f) && strcmp(line, FORMAT) == 0;
	int count = 0;
	if (ok)
		ok = fgets(line, sizeof(line), f) && sscanf(line, "%d", &count) == 1 && count > 0 && count <= BOOKDOWNLOAD_MAX_PARTS;
	for (int i = 0; ok && i < count; i++) {
		char name[80], extra;
		long long audio, tags, actual_audio, actual_tags;
		ok = fgets(line, sizeof(line), f) && sscanf(line, "%79s %lld %lld %c", name, &audio, &tags, &extra) == 3 && part_sizes(directory, name, &actual_audio, &actual_tags) && audio == actual_audio && tags == actual_tags;
		if (ok && i == 0)
			snprintf(first, sizeof(first), "%s", name);
	}
	ok = ok && fgetc(f) == EOF && !ferror(f);
	if (fclose(f) != 0)
		ok = false;
	if (ok && book_out && size) {
		int n = count == 1 ? snprintf(book_out, size, "%s/%s", directory, first) : snprintf(book_out, size, "%s", directory);
		ok = n > 0 && (size_t)n < size;
	}
	return ok;
}

static bool owned_stage(const char *dir) {
	char path[512], line[32];
	if (!join(path, sizeof(path), dir, OWNER))
		return false;
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(line, sizeof(line), f) && strcmp(line, FORMAT) == 0;
	fclose(f);
	return ok;
}

void bookdownload_abort(bookdownload_t *book) {
	if (!book->owned || !owned_stage(book->folder))
		return;
	DIR *d = opendir(book->folder);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 || strcmp(e->d_name, OWNER) == 0)
			continue;
		// Staging is flat. Never recurse into unexpected directories.
		unlinkat(dirfd(d), e->d_name, 0);
	}
	closedir(d);
	char path[512];
	if (join(path, sizeof(path), book->folder, OWNER))
		unlink(path);
	rmdir(book->folder);
	book->owned = false;
}

bool bookdownload_begin(bookdownload_t *book, const char *directory) {
	if (!book || !directory)
		return false;
	memset(book, 0, sizeof(*book));
	if (strlen(directory) + 7 >= sizeof(book->folder))
		return false;
	snprintf(book->final, sizeof(book->final), "%s", directory);
	struct stat st;
	if (lstat(directory, &st) == 0 || errno != ENOENT)
		return false; // Never replace an existing book.
	const char *slash = strrchr(directory, '/');
	if (!slash)
		return false;
	snprintf(book->folder, sizeof(book->folder), "%.*s/.%s.part", (int)(slash - directory), directory, slash + 1);
	if (lstat(book->folder, &st) == 0) {
		if (!S_ISDIR(st.st_mode) || !owned_stage(book->folder))
			return false;
		book->owned = true;
		bookdownload_abort(book); // A previous interrupted job, never a published book.
	}
	if (mkdir(book->folder, 0755) != 0)
		return false;
	char path[512];
	if (!join(path, sizeof(path), book->folder, OWNER))
		return false;
	FILE *f = fopen(path, "wx");
	bool ok = f && fputs(FORMAT, f) >= 0;
	if (f && fclose(f) != 0)
		ok = false;
	if (!ok) {
		unlink(path);
		rmdir(book->folder);
		return false;
	}
	book->owned = true;
	return true;
}

bool bookdownload_record(bookdownload_t *book, const char *filename) {
	long long audio, tags;
	if (!book->owned || book->count >= BOOKDOWNLOAD_MAX_PARTS || !part_sizes(book->folder, filename, &audio, &tags))
		return false;
	for (int i = 0; i < book->count; i++)
		if (strcmp(book->files[i], filename) == 0)
			return false;
	snprintf(book->files[book->count++], sizeof(book->files[0]), "%s", filename);
	return true;
}

bool bookdownload_publish(bookdownload_t *book, int expected) {
	if (!book->owned || expected <= 0 || book->count != expected)
		return false;
	char path[512];
	if (!join(path, sizeof(path), book->folder, MANIFEST))
		return false;
	FILE *f = fopen(path, "w");
	if (!f)
		return false;
	bool ok = fprintf(f, FORMAT "%d\n", book->count) > 0;
	for (int i = 0; ok && i < book->count; i++) {
		long long audio, tags;
		ok = part_sizes(book->folder, book->files[i], &audio, &tags) && fprintf(f, "%s\t%lld\t%lld\n", book->files[i], audio, tags) > 0;
	}
	if (fclose(f) != 0)
		ok = false;
	struct stat st;
	if (!ok || lstat(book->final, &st) == 0 || errno != ENOENT || rename(book->folder, book->final) != 0)
		return false;
	book->owned = false;
	return true;
}
