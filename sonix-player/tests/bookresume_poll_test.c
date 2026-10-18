// Host regression: the player poll must not wait on the audiobook database.
// cc -std=gnu11 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I. tests/bookresume_poll_test.c src/gui/shell/bookresume.c -Wl,--gc-sections -o /tmp/bookresume-poll && /tmp/bookresume-poll
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/gui/shell/bookresume.h"
#include "src/system/core/config.h"
#include "src/system/library/audiobookdb.h"
#include "src/system/playback/audiobook.h"

static unsigned serial = 1;
static int checks, reads, revisions, saves, refreshes;
static bool is_book;
static char book[512], file[512];
static struct { char key[32], value[128]; } values[8];
static int value_count;

unsigned audiobook_serial(void) { return serial; }
unsigned audiobookdb_revision(void) { revisions++; return 1; }
bool audiobook_current_book(char *out, size_t n, char *part, size_t m) {
	checks++;
	if (!is_book) return false;
	snprintf(out, n, "%s", book); snprintf(part, m, "%s", file);
	return true;
}
bool audiobookdb_book_info(const char *path, char *title, size_t size, char *author, size_t author_size, char **summary) {
	(void)path; (void)author; (void)author_size; (void)summary;
	reads++;
	snprintf(title, size, "%s", "A book actually playing");
	return true;
}
const char *config_get(const char *section, const char *key, const char *fallback) {
	assert(!strcmp(section, "home"));
	for (int i = 0; i < value_count; i++) if (!strcmp(values[i].key, key)) return values[i].value;
	return fallback;
}
void config_set(const char *section, const char *key, const char *value) {
	assert(!strcmp(section, "home") && strlen(value) < 128);
	int i;
	for (i = 0; i < value_count; i++) if (!strcmp(values[i].key, key)) break;
	assert(i < 8);
	if (i == value_count) value_count++;
	snprintf(values[i].key, sizeof(values[i].key), "%s", key);
	snprintf(values[i].value, sizeof(values[i].value), "%s", value);
}
bool config_save(void) { saves++; return true; }
void main_menu_refresh(void) { refreshes++; }

static void expect_selected(const char *expected) {
	char combined[640] = "";
	for (int i = 0; i < 5; i++) {
		char key[32];
		snprintf(key, sizeof(key), i ? "resume_book_%d" : "resume_book", i);
		const char *part = config_get("home", key, "");
		assert(mbstowcs(NULL, part, 0) != (size_t)-1);
		strcat(combined, part);
	}
	assert(!strcmp(combined, expected));
}
int main(void) {
	assert(setlocale(LC_CTYPE, "C.UTF-8"));
	device_state_t state = {.status = AUDIO_STATUS_PLAYING};
	snprintf(state.current_file, sizeof(state.current_file), "/Music/ordinary.flac");
	bookresume_note(&state);
	for (int i = 0; i < 1000; i++) bookresume_note(&state);
	assert(checks == 1 && reads == 0 && revisions == 0 && saves == 0);
	is_book = true; serial++;
	snprintf(book, sizeof(book), "/Audiobooks/");
	for (int i = 0; i < 12; i++) strcat(book, "Книги - a long folder/");
	strcat(book, "chapter.flac");
	snprintf(file, sizeof(file), "%s", book);
	snprintf(state.current_file, sizeof(state.current_file), "%s", file);
	bookresume_note(&state);
	expect_selected(book);
	assert(reads == 1 && saves == 1 && refreshes == 1);
	for (int i = 0; i < 1000; i++) bookresume_note(&state);
	assert(checks == 2 && reads == 1 && revisions == 0 && saves == 1);
	serial++;
	snprintf(file, sizeof(file), "/Audiobooks/another-part.flac");
	snprintf(state.current_file, sizeof(state.current_file), "%s", file);
	bookresume_note(&state);
	assert(reads == 1 && saves == 1);
	serial++;
	snprintf(book, sizeof(book), "/Audiobooks/short.flac");
	snprintf(file, sizeof(file), "%s", book);
	snprintf(state.current_file, sizeof(state.current_file), "%s", file);
	bookresume_note(&state);
	expect_selected(book);
	assert(saves == 2 && reads == 2 && revisions == 0);
	const int lengths[] = {119,120,121,127,128,239,240,479,480,511};
	for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		memset(book, 'a', (size_t)lengths[i]);
		book[0] = '/'; book[lengths[i]] = 0;
		snprintf(file, sizeof(file), "%s", book);
		snprintf(state.current_file, sizeof(state.current_file), "%s", file);
		serial++;
		bookresume_note(&state);
		expect_selected(book);
	}
	puts("PASS: stable poll performs no DB access/write; chapter changes preserve selection; UTF-8 long/short paths round-trip");
}
