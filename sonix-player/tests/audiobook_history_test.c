// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I.
// tests/audiobook_history_test.c -Wl,--gc-sections -pthread -o /tmp/book_history_test
#define clock_gettime history_test_clock_gettime
#include "src/system/playback/audiobook.c"
#undef clock_gettime
#include <assert.h>

static uint64_t test_ms;
static int marks;
static long long last_mark;
static long test_speed = 1000;

int history_test_clock_gettime(clockid_t id, struct timespec *out) {
	assert(id == CLOCK_MONOTONIC);
	out->tv_sec = test_ms / 1000;
	out->tv_nsec = (test_ms % 1000) * 1000000;
	return 0;
}
long config_get_int(const char *section, const char *key, long fallback) {
	(void)section;
	(void)key;
	(void)fallback;
	return test_speed;
}
void audiobookdb_queue_chapter_done(const char *book, const char *file, long long start_ms) {
	assert(!strcmp(book, "/book") && !strcmp(file, "/book/part.flac"));
	marks++;
	last_mark = start_ms;
}
static void reset_book(void) {
	clear_locked();
	current_is_book = true;
	strcpy(current_book, "/book");
	strcpy(current_path, "/book/part.flac");
	chapter_count = 3;
	chapters = calloc(3, sizeof(*chapters));
	assert(chapters);
	for (int i = 0; i < 3; i++)
		chapters[i].start = i * 10;
	marks = 0;
	test_ms = 0;
	test_speed = 1000;
}
int main(void) {
	reset_book();
	audiobook_listen_progress(0, 30, true, 1);
	test_ms = 11000;
	audiobook_listen_progress(11, 30, true, 1);
	assert(marks == 1 && audiobook_chapter_listened(0));
	audiobook_listen_seek();
	test_ms = 12000;
	audiobook_listen_progress(25, 30, true, 1);
	assert(!audiobook_chapter_listened(1));
	test_ms = 17000;
	audiobook_listen_end(1);
	assert(marks == 2 && last_mark == 20000 && audiobook_chapter_listened(2));
	audiobook_listen_end(1);
	assert(marks == 2);
	reset_book();
	audiobook_listen_progress(0, 30, true, 1);
	test_ms = 1000;
	audiobook_listen_progress(25, 30, true, 1);
	assert(marks == 0); // An unannounced impossible jump is not listening.
	audiobook_listen_seek();
	audiobook_listen_end(1);
	assert(marks == 0); // Seeking to EOF cannot mark the final chapter.
	reset_book();
	audiobook_listen_progress(9.8, 30, true, 1);
	test_ms = 500;
	audiobook_listen_progress(10.1, 30, false, 1);
	assert(marks == 1); // Pausing just after a natural boundary preserves it.
	audiobook_listen_progress(25, 30, false, 1);
	assert(marks == 1 && !audiobook_chapter_listened(1));
	reset_book();
	test_speed = 2000;
	audiobook_listen_progress(0, 30, true, 1);
	test_ms = 10500;
	audiobook_listen_progress(21, 30, true, 1);
	assert(marks == 2 && audiobook_chapter_listened(1));
	reset_book();
	part_count = 1;
	part_index = 0;
	parts = calloc(1, sizeof(*parts));
	assert(parts);
	strcpy(parts[0].path, current_path);
	audiobook_listen_progress(28, 30, true, 1);
	test_ms = 2000;
	audiobook_listen_end(1);
	assert(audiobook_part_listened(0) && last_mark == -1);
	reset_book();
	audiobook_listen_progress(29, 30, true, 1);
	test_ms = 1000;
	audiobook_listen_progress(30, 30, true, 1);
	audiobook_listen_end(1);
	assert(audiobook_chapter_listened(2));
	reset_book();
	audiobook_listen_progress(9.8, 30, true, 1);
	test_ms = 500;
	audiobook_listen_progress(10.2, 30, true, 2);
	assert(marks == 0); // Seek completed entirely between polls: new serial.
	reset_book();
	audiobook_listen_progress(9.8, 30, true, 1);
	test_ms = 500;
	audiobook_listen_progress(9.8, 30, false, 2);
	audiobook_listen_progress(10.2, 30, true, 2);
	assert(marks == 0); // Pending seek never re-arms the old baseline.
	audiobook_listen_end(3);
	assert(marks == 0); // Seek-to-EOF before another progress poll.
	clear_locked();
	puts("PASS natural boundaries, resume, pause, 2x speed, seek/EOF rejection and folder completion");
	return 0;
}
