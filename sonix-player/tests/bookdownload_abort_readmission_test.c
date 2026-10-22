// gcc -std=gnu11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Werror -I. tests/bookdownload_abort_readmission_test.c -o /tmp/book-abort
// Real admitted book transaction; no ADB, network or real card.
#define _GNU_SOURCE
#include "src/system/streaming/bookdownload.c"
#include <assert.h>

static char root[512], stage[512], saved[600];
static bool armed, replaced;
static unsigned admissions;
static int admit(void *unused) {
	(void)unused;
	if (armed && ++admissions == 2) {
		assert(!rename(stage, saved));
		assert(!mkdir(stage, 0700));
		replaced = true;
	}
	return open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
int main(void) {
	alarm(10);
	char tmp[] = "/tmp/sonix-book-abort-admit-XXXXXX";
	assert(mkdtemp(tmp));
	snprintf(root, sizeof(root), "%s", tmp);
	char final[512];
	assert(bookdownload_path(root, "Books", "yandex", "123", final, sizeof(final)));
	bookdownload_t *book = calloc(1, sizeof(*book));
	assert(book);
	bookdownload_io_t io = {.open_root = admit};
	assert(bookdownload_begin_admitted(book, root, final, &io));
	snprintf(stage, sizeof(stage), "%s", book->folder);
	snprintf(saved, sizeof(saved), "%s-before-readmission", stage);
	armed = true;
	bookdownload_abort(book);
	struct stat st;
	bool foreign_exists = stat(stage, &st) == 0 && S_ISDIR(st.st_mode);
	printf("second_abort_admission_replaced=%d foreign_empty_stage_preserved=%d expected=1 owned=%d\n", replaced, foreign_exists, book->owned);
	assert(replaced);
	free(book);
	return foreign_exists ? 0 : 1;
}
