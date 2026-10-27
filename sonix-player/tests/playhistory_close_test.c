// Real accepted history must survive the ordinary prepared-close boundary.
// The missing-prepare control retains the former losing call order.
#define _GNU_SOURCE
#include "src/system/playback/playhistory.c"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void history_close_fixture_open(const char *path);
int history_close_fixture_rows(void);
int history_close_fixture_imported(void);
bool history_close_fixture_in_transaction(void);
void history_close_fixture_rollback_import(void);

static pthread_t fixture_gui;

void thread_be_low_priority(const char *name) { assert(name && !pthread_equal(fixture_gui, pthread_self())); }

bool clock_is_set(void) { return true; }

bool audio_get_listening_progress(double *position, double *length, unsigned *serial) {
	(void)position;
	(void)length;
	if (serial)
		*serial = 1;
	return true;
}

void audio_get_current_file(char *out, size_t size) { snprintf(out, size, "/fixture-card/heard.flac"); }

int main(int argc, char **argv) {
	assert(argc == 4);
	fixture_gui = pthread_self();
	bool predrained = !strcmp(argv[1], "predrained-control");
	bool replacement = !strcmp(argv[1], "replacement");
	bool missing_prepare = !strcmp(argv[1], "missing-prepare");
	assert(predrained || replacement || missing_prepare || !strcmp(argv[1], "ordinary-close"));
	history_close_fixture_open(argv[2]);
	unsigned origin = library_database_epoch();
	library_playlist_writer_t *import = library_playlist_write_begin("History close fixture");
	assert(import && history_close_fixture_in_transaction());

	// A real first PCM block is confirmed while the import owns the connection.
	playhistory_started("/fixture-card/heard.flac", "Confirmed listen", "Artist", false, false);
	playhistory_heard("/fixture-card/heard.flac");
	playhistory_flush();
	pthread_mutex_lock(&live_lock);
	assert(queued == 1 && first && first->dirty && first->id <= 0);
	assert(!writer_busy && !writer_requested && writer_saved == 0);
	pthread_mutex_unlock(&live_lock);
	assert(history_close_fixture_rows() == 0);
	printf("BEFORE close: origin_epoch=%u pending=1 committed=0 foreign_transaction=1\n", origin);
	fflush(stdout);

	// Positive control changes only ordering: the same actual import rollback
	// runs before the storage orchestrator's final history flush.
	if (predrained)
		history_close_fixture_rollback_import();

	// Storage leaves the old epoch usable until application persistence drains,
	// but no new long-lived import may take over the connection in between.
	if (!missing_prepare) {
		library_prepare_close();
		assert(library_database_epoch() == origin);
		assert(!history_close_fixture_in_transaction());
		assert(!library_playlist_write_begin("Late import"));
	}
	playhistory_flush();
	if (!missing_prepare) {
		library_history_item_t *items = NULL;
		assert(library_history_items(&items) == 1);
		free(items);
		assert(history_close_fixture_imported() == 0);
	}
	library_close();
	assert(library_database_epoch() == 0);
	assert(!library_playlist_write_end(import, false));

	// A replacement is a separate empty database. A late old event must never
	// be adopted there, even if a shutdown fix preserves it for the old owner.
	history_close_fixture_open(replacement ? argv[3] : argv[2]);
	playhistory_poll();
	playhistory_flush();
	int rows = history_close_fixture_rows();
	pthread_mutex_lock(&live_lock);
	unsigned pending = queued;
	pthread_mutex_unlock(&live_lock);
	printf("AFTER reopen: current_epoch=%u pending=%u committed=%d replacement=%d\n", library_database_epoch(), pending, rows, replacement);
	bool okay = replacement ? rows == 0 && pending == 0 : rows == 1 && pending == 0;
	printf("%s %s: expected committed=%d, got=%d\n", okay ? "PASS" : "FAIL", argv[1], replacement ? 0 : 1, rows);
	library_playlist_writer_t *reopened = library_playlist_write_begin("Reopened import");
	assert(reopened && history_close_fixture_in_transaction());
	assert(!library_playlist_write_end(reopened, false));
	library_prepare_close();
	playhistory_shutdown();
	library_close();
	return okay ? 0 : 1;
}
