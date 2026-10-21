#include "src/system/core/config.h"
/* Real scanner and SQLite resume persistence; metadata readers are fixtures.
 * cc -std=gnu11 -O1 -Wall -Wextra -I. -ffunction-sections -fdata-sections
 * tests/audiobook_import_resume_test.c build_host/system/db/sqlite3_impl.o
 * -Wl,--gc-sections -pthread -lm -ldl -o /tmp/book-resume && /tmp/book-resume
 */
#include "src/system/library/audiobookdb.c"
#include <assert.h>

const char *stub_config_get(const char *section, const char *key, const char *fallback) {
	(void)section;
	(void)key;
	(void)fallback;
	return "Books/Audio";
}
const char *config_get_copy(const char *section, const char *key, const char *fallback, char *out, size_t size) {
 const char *v=stub_config_get(section,key,fallback); if(v && size) snprintf(out,size,"%s",v); return v ? out : NULL;
}

void file_identity_read(const char *path, file_identity_t *out) {
	(void)path;
	memset(out, 0, sizeof(*out));
}
void thread_be_background(const char *name) { (void)name; }
void thread_be_low_priority(const char *name) { (void)name; }
bool playlist_is_junk_name(const char *name) {
	(void)name;
	return false;
}
bool has_extension(const char *name, const char *ext) {
	size_t n = strlen(name), e = strlen(ext);
	return n >= e && strcasecmp(name + n - e, ext) == 0;
}
mp4_file_t *mp4_open(const char *path) {
	(void)path;
	return NULL;
}
void mp4_close(mp4_file_t *file) { (void)file; }
int mp4_chapter_count(const mp4_file_t *file) {
	(void)file;
	return 0;
}
bool id3chap_present(const char *path) { return strstr(path, "/0002-") != NULL; }
bool vorbischap_present(const char *path) {
	(void)path;
	return false;
}
int library_collate_listorder(void *u, int na, const void *a, int nb, const void *b) {
	(void)u;
	int cmp = strncmp(a, b, (size_t)(na < nb ? na : nb));
	return cmp ? cmp : na - nb;
}
void metadata_read(const char *path, song_metadata_t *m) {
	memset(m, 0, sizeof(*m));
	snprintf(m->title, sizeof(m->title), "%s", strrchr(path, '/') + 1);
	strcpy(m->album, "Book title");
	strcpy(m->artist, "Narrator credit");
	strcpy(m->album_artist, "Ambiguous embedded credit");
}
char *metadata_read_with_description(const char *path, song_metadata_t *m) {
	metadata_read(path, m);
	return NULL;
}
static void put(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	assert(f);
	assert(fputs(text, f) >= 0 && fclose(f) == 0);
}
static int seen;
static bool ordered(const char *path, const char *title, void *unused) {
	(void)title;
	(void)unused;
	seen++;
	char ending[40];
	snprintf(ending, sizeof(ending), "/%04d-%d.mp3", seen, 10 + seen);
	assert(strstr(path, ending));
	return true;
}
int main(void) {
	char temp[] = "/tmp/book-resume-XXXXXX", shelf[512], book[512], path[512], part[512], dbpath[512];
	assert(mkdtemp(temp));
	snprintf(shelf, sizeof(shelf), "%s/Audio", temp);
	assert(mkdir(shelf, 0700) == 0);
	snprintf(book, sizeof(book), "%s/Audio/yandex-38681851", temp);
	assert(mkdir(book, 0700) == 0);
	snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/.sonix-book", temp);
	put(path, "SONIX-BOOK-1\n");
	for (int i = 1; i <= 13; i++) {
		snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/%04d-%d.mp3", temp, i, 10 + i);
		put(path, "audio fixture");
	}
	snprintf(dbpath, sizeof(dbpath), "%s/books.db", temp);
	assert(audiobookdb_open(dbpath));
	snprintf(scan_root, sizeof(scan_root), "%s", shelf);
	scan_running = true;
	scan_cancel = false;
	// A publication during an active scan coalesces one following pass.
	audiobookdb_request_scan(shelf);
	assert(scan_pending[0]);
	scan_thread_func(NULL);
	assert(!scan_running && !scan_pending[0] && audiobookdb_count() == 1);
	assert(audiobookdb_parts_for_each(book, ordered, NULL) == 13 && seen == 13);
	char author[256];
	assert(audiobookdb_book_info(book, NULL, 0, author, sizeof(author), NULL));
	assert(!author[0]); // Service artist credit must not become an invented author.
	snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/0001-11.mp3.tags", temp);
	put(path, "album_artist=Confirmed author\n");
	scan_running = true;
	scan_cancel = false;
	scan_found = 0;
	scan_thread_func(NULL);
	assert(audiobookdb_book_info(book, NULL, 0, author, sizeof(author), NULL) && strcmp(author, "Confirmed author") == 0);
	put(path, "album_artist=\n");
	scan_running = true;
	scan_cancel = false;
	scan_found = 0;
	scan_thread_func(NULL);
	assert(audiobookdb_book_info(book, NULL, 0, author, sizeof(author), NULL) && !author[0]);
	snprintf(part, sizeof(part), "%s/Audio/yandex-38681851/0002-12.mp3", temp);
	audiobookdb_save_position(book, part, 123.5);
	assert(audiobookdb_bookmark_add(book, part, 123.5, "Mark") == AUDIOBOOK_BOOKMARK_SAVED);
	audiobookdb_mark_finished(book);
	scan_running = true;
	scan_cancel = false;
	scan_found = 0;
	scan_thread_func(NULL);
	double seconds = 0;
	assert(audiobookdb_get_position(book, path, sizeof(path), &seconds));
	assert(strcmp(path, part) == 0 && seconds == 123.5 && audiobookdb_is_finished(book));
	audiobookdb_close();
	assert(audiobookdb_open(dbpath));
	assert(audiobookdb_get_position(book, path, sizeof(path), &seconds) && seconds == 123.5 && strcmp(path, part) == 0);
	sqlite3_stmt *stmt = NULL;
	assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM AUDIOBOOK_BOOKMARKS", -1, &stmt, NULL) == SQLITE_OK);
	assert(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 1);
	sqlite3_finalize(stmt);
	audiobookdb_close();
	for (int i = 1; i <= 13; i++) {
		snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/%04d-%d.mp3", temp, i, 10 + i);
		assert(unlink(path) == 0);
	}
	snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/.sonix-book", temp);
	assert(unlink(path) == 0);
	snprintf(path, sizeof(path), "%s/Audio/yandex-38681851/0001-11.mp3.tags", temp);
	assert(unlink(path) == 0);
	assert(rmdir(book) == 0 && rmdir(shelf) == 0);
	assert(unlink(dbpath) == 0);
	snprintf(path, sizeof(path), "%s/books.db-journal", temp);
	unlink(path);
	assert(rmdir(temp) == 0);
	puts("Imported book order, coalesced scan, resume and bookmarks after reopen: PASS");
}
