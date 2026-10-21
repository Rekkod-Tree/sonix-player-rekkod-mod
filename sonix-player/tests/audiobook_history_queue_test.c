// cc -O1 -g -std=gnu11 -ffunction-sections -fdata-sections -I.
// tests/audiobook_history_queue_test.c src/system/db/sqlite3_impl.c
// -Wl,--gc-sections -pthread -ldl -lm -o /tmp/book_history_queue_test
#include "src/system/library/audiobookdb.c"
#include <assert.h>

void thread_be_background(const char *name) { (void)name; }
static int row_count(void) {
	sqlite3_stmt *s = NULL;
	assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM AUDIOBOOK_CHAPTER_DONE", -1, &s, NULL) == SQLITE_OK);
	assert(sqlite3_step(s) == SQLITE_ROW);
	int count = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);
	return count;
}
int main(void) {
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	assert(sqlite3_exec(db, "CREATE TABLE AUDIOBOOK_CHAPTER_DONE(book TEXT,file TEXT,start_ms INTEGER,PRIMARY KEY(book,file,start_ms))", NULL, NULL, NULL) == SQLITE_OK);
	card_epoch = 1;
	// Force the bounded queue's fallback without starting a worker yet.
	queue_started = true;
	chapter_count = CHAPTER_WRITE_QUEUE;
	audiobookdb_queue_chapter_done("/book", "/part", 0);
	assert(row_count() == 1);
	audiobookdb_queue_chapter_done("/book", "/part", 0);
	assert(row_count() == 1);
	queued_position_t old = {.chapter_done = true, .chapter_start_ms = 1000, .epoch = 1};
	strcpy(old.book, "/book");
	strcpy(old.file, "/part");
	card_epoch = 2;
	write_queued(&old);
	assert(row_count() == 1);
	chapter_count = 0;
	chapter_head = 0;
	queue_started = false;
	for (int i = 1; i <= 64; i++)
		audiobookdb_queue_chapter_done("/book", "/part", i * 1000);
	position_queue_drain();
	assert(row_count() == 65 && !queue_busy && !queued.pending && chapter_count == 0);
	pthread_mutex_lock(&db_lock);
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	pthread_mutex_unlock(&db_lock);
	puts("PASS queue-full fallback, idempotence, old-card rejection and 64-mark drain before close");
	return 0;
}
