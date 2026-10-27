// Real SQLite/library checkpoint path; no device or user card is involved.
// Build with -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread
// and src/system/db/sqlite3_impl.c -ldl -lm. Run each named case separately.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int denied_stat;
static int fixture_stat(const char *path, struct stat *st) {
	if (denied_stat && !strncmp(path, "/sonix-fixture-only/", 20)) {
		errno = denied_stat;
		return -1;
	}
	return stat(path, st);
}
static bool refuse_start;
static unsigned starts;
static int start_writer(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
	starts++;
	return refuse_start ? EAGAIN : pthread_create(t, a, fn, arg);
}
#define pthread_create start_writer
#define stat(path, st) fixture_stat(path, st)
#include "src/system/library/library.c"
#undef pthread_create
#undef stat

static pthread_t gui_thread;
static atomic_uint commits;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool gate_blocked, gate_entered;

void thread_be_low_priority(const char *name) { assert(name && !pthread_equal(gui_thread, pthread_self())); }
void covertone_library_changed(void) {}

static int commit_hook(void *unused) {
	(void)unused;
	assert(!pthread_equal(gui_thread, pthread_self()));
	atomic_fetch_add(&commits, 1);
	pthread_mutex_lock(&gate_lock);
	gate_entered = true;
	pthread_cond_broadcast(&gate_cond);
	while (gate_blocked)
		pthread_cond_wait(&gate_cond, &gate_lock);
	pthread_mutex_unlock(&gate_lock);
	return 0;
}

static int scalar(const char *sql) {
	pthread_mutex_lock(&db_lock);
	sqlite3_stmt *s = NULL;
	assert(sqlite3_prepare_v2(db, sql, -1, &s, NULL) == SQLITE_OK);
	assert(sqlite3_step(s) == SQLITE_ROW);
	int value = sqlite3_column_int(s, 0);
	assert(sqlite3_finalize(s) == SQLITE_OK);
	pthread_mutex_unlock(&db_lock);
	return value;
}

static void setup(void) {
	gui_thread = pthread_self();
	assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
	for (size_t i = 0; i < sizeof(SCHEMA) / sizeof(SCHEMA[0]); i++) {
		if (SCHEMA[i] && strstr(SCHEMA[i], "CREATE TABLE IF NOT EXISTS PLAYBACK_"))
			assert(exec(SCHEMA[i]));
	}
	assert(exec("INSERT INTO PLAYBACK_QUEUE_STATE VALUES(0,0,0,0)"));
	atomic_store(&database_epoch, 10);
	atomic_store(&database_online, true);
}

static void wait_entered(void) {
	for (int i = 0; i < 5000; i++) {
		pthread_mutex_lock(&gate_lock);
		bool entered = gate_entered;
		pthread_mutex_unlock(&gate_lock);
		if (entered)
			return;
		usleep(1000);
	}
	assert(!"writer did not reach commit");
}

static void coalesce(void) {
	sqlite3_commit_hook(db, commit_hook, NULL);
	gate_blocked = true;
	library_checkpoint_position_at(10, "/first.flac", 10);
	library_checkpoint_queue_at(10, 1, false);
	wait_entered();
	// The actual SQLite writer is blocked at commit with db_lock held. New
	// playback admission must still return, and only retain the latest values.
	int64_t before = checkpoint_now();
	for (int i = 2; i <= 10000; i++) {
		library_checkpoint_position_at(10, "/latest.flac", i);
		library_checkpoint_queue_at(10, i, false);
	}
	assert(checkpoint_now() - before < 1000);
	pthread_mutex_lock(&gate_lock);
	gate_blocked = false;
	pthread_cond_broadcast(&gate_cond);
	pthread_mutex_unlock(&gate_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT position FROM PLAYBACK_STATE") == 10000);
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 10000);
	assert(scalar("SELECT path='/latest.flac' FROM PLAYBACK_STATE") == 1);
	assert(atomic_load(&commits) == 2);
	unsigned idle_commits = atomic_load(&commits);
	for (int i = 0; i < 10000; i++)
		library_checkpoint_poll();
	usleep(200000);
	assert(atomic_load(&commits) == idle_commits);
	assert(starts == 1);
}

static void epoch_case(void) {
	// Keep the first worker outside SQLite until the database identity moves.
	pthread_mutex_lock(&db_lock);
	library_checkpoint_position_at(10, "/old-card.flac", 25);
	library_checkpoint_queue_at(10, 9, false);
	atomic_fetch_add(&database_epoch, 1);
	pthread_mutex_unlock(&db_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT count(*) FROM PLAYBACK_STATE") == 0);
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
	// A late producer may still hold the old playback/queue context. Admission
	// must not silently replace its expected epoch with the current database.
	library_checkpoint_position_at(10, "/late-old-card.flac", 40);
	library_checkpoint_queue_at(10, 8, false);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT count(*) FROM PLAYBACK_STATE") == 0);
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
	library_checkpoint_position_at(11, "/new-card.flac", 35);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT path='/new-card.flac' FROM PLAYBACK_STATE") == 1);
	// A late file deletion must not prune a newly opened card's index.
	assert(exec("CREATE TABLE MEDIA_TABLE(path TEXT)"));
	assert(exec("INSERT INTO MEDIA_TABLE VALUES('/sonix-fixture-only/missing.flac')"));
	const char *gone[] = {"/sonix-fixture-only/missing.flac"};
	assert(library_forget_deleted_at(10, gone, 1) == 0);
	assert(scalar("SELECT count(*) FROM MEDIA_TABLE") == 1);
	denied_stat = EIO;
	assert(library_forget_deleted_at(11, gone, 1) == 0);
	assert(scalar("SELECT count(*) FROM MEDIA_TABLE") == 1);
	denied_stat = EACCES;
	assert(library_forget_deleted_at(11, gone, 1) == 0);
	assert(scalar("SELECT count(*) FROM MEDIA_TABLE") == 1);
}

static void generation_case(void) {
	library_index_spec_t spec = {.valid = true, .kind = LIBRARY_LIST_TRACKS};
	const char *extra[] = {"/added.flac"};
	int slots[] = {4};
	library_queue_save_query(&spec, 0, NULL, NULL, 0);
	refuse_start = true;
	library_checkpoint_queue_at(10, 99, true);
	library_queue_save_query(&spec, 2, extra, slots, 1);
	refuse_start = false;
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_QUERY") == 2);
	assert(scalar("SELECT count(*) FROM PLAYBACK_QUEUE_STATE") == 0);
	// Index-only updates must not rewrite the query or appended rows.
	sqlite3_commit_hook(db, commit_hook, NULL);
	library_checkpoint_queue_at(10, 5, true);
	library_checkpoint_position_at(10, "/track.flac", 15);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_QUERY") == 5);
	assert(scalar("SELECT count(*) FROM PLAYBACK_QUEUE WHERE path='/added.flac' AND slot=4") == 1);
	assert(atomic_load(&commits) == 1);
}

static void transaction_case(void) {
	assert(exec("BEGIN"));
	library_checkpoint_position_at(10, "/track.flac", 15);
	library_checkpoint_queue_at(10, 5, false);
	assert(!library_checkpoint_flush());
	assert(!sqlite3_get_autocommit(db));
	assert(scalar("SELECT count(*) FROM PLAYBACK_STATE") == 0);
	assert(exec("ROLLBACK"));
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 5);
	assert(exec("CREATE TRIGGER fail_checkpoint BEFORE UPDATE ON PLAYBACK_QUEUE_STATE BEGIN SELECT RAISE(FAIL,'fixture'); END"));
	library_checkpoint_position_at(10, "/new.flac", 27);
	library_checkpoint_queue_at(10, 6, false);
	assert(!library_checkpoint_flush());
	assert(scalar("SELECT position FROM PLAYBACK_STATE") == 15);
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 5);
	assert(exec("DROP TRIGGER fail_checkpoint"));
	assert(library_checkpoint_flush());
	assert(scalar("SELECT position FROM PLAYBACK_STATE") == 27);
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 6);
}

static void refusal_case(void) {
	refuse_start = true;
	sqlite3_commit_hook(db, commit_hook, NULL);
	library_checkpoint_position_at(10, "/pending.flac", 23);
	for (int i = 0; i < 10000; i++)
		library_checkpoint_poll();
	assert(starts == 1 && atomic_load(&commits) == 0);
	assert(scalar("SELECT count(*) FROM PLAYBACK_STATE") == 0);
	refuse_start = false;
	assert(library_checkpoint_flush());
	assert(starts == 2 && scalar("SELECT position FROM PLAYBACK_STATE") == 23);
}

static void close_case(void) {
	library_checkpoint_position_at(10, "/before-close.flac", 23);
	atomic_store(&database_online, false); // library_close shuts admission first.
	library_checkpoint_position_at(10, "/too-late.flac", 0);
	library_checkpoint_shutdown();
	assert(scalar("SELECT path='/before-close.flac' FROM PLAYBACK_STATE") == 1);
	assert(!checkpoint_started && !checkpoint_dirty());
	atomic_fetch_add(&database_epoch, 1);
	atomic_store(&database_online, true);
	library_checkpoint_position_at(11, "/reopened.flac", 31);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT path='/reopened.flac' FROM PLAYBACK_STATE") == 1);
}

static void queue_entry_case(void) {
	assert(exec("INSERT INTO PLAYBACK_QUEUE(idx,path) VALUES(0,'/different-neighbour.flac'),(1,'/current.flac'),(2,'/last.flac')"));
	library_checkpoint_queue_entry_at(10, 1, 3, false, "/current.flac");
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 1);
	assert(scalar("SELECT path='/different-neighbour.flac' FROM PLAYBACK_QUEUE WHERE idx=0") == 1);
	assert(scalar("SELECT count(*) FROM PLAYBACK_QUEUE") == 3);
	// Every offset is validated against this DB. No whole-queue identity is
	// inferred from the current row matching on a previous request.
	for (int invalid = 0; invalid < 5; invalid++) {
		assert(exec("UPDATE PLAYBACK_QUEUE_STATE SET pos=0,updated_at=0"));
		if (invalid == 4)
			assert(exec("INSERT INTO PLAYBACK_QUEUE_QUERY(id) VALUES(0)"));
		library_checkpoint_queue_entry_at(10, invalid == 1 ? 2 : 1, invalid == 2 ? 4 : 3, invalid == 3, invalid == 0 ? "/another-card.flac" : "/current.flac");
		assert(library_checkpoint_flush()); // Mismatch is disposed, not retried.
		assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
		assert(scalar("SELECT updated_at FROM PLAYBACK_QUEUE_STATE") == 0);
		assert(exec("DELETE FROM PLAYBACK_QUEUE_QUERY"));
	}
	// Matching at admission is insufficient: the worker must check again after
	// obtaining the DB lock, inside the same statement as the position update.
	pthread_mutex_lock(&db_lock);
	library_checkpoint_queue_entry_at(10, 1, 3, false, "/current.flac");
	assert(exec("UPDATE PLAYBACK_QUEUE SET path='/replacement.flac' WHERE idx=1"));
	pthread_mutex_unlock(&db_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
	assert(exec("UPDATE PLAYBACK_QUEUE SET path='/current.flac' WHERE idx=1"));
	// A new queue generation or DB epoch must reject a delayed command even
	// when the visible slot still happens to name the same path.
	pthread_mutex_lock(&db_lock);
	library_checkpoint_queue_entry_at(10, 1, 3, false, "/current.flac");
	atomic_fetch_add(&checkpoint_queue_generation, 1);
	pthread_mutex_unlock(&db_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
	pthread_mutex_lock(&db_lock);
	library_checkpoint_queue_entry_at(10, 1, 3, false, "/current.flac");
	atomic_fetch_add(&database_epoch, 1);
	pthread_mutex_unlock(&db_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 0);
	// Coalescing a normal owned-queue offset after a guarded request removes
	// the old validation fields instead of accidentally applying them again.
	pthread_mutex_lock(&db_lock);
	library_checkpoint_queue_entry_at(11, 1, 3, false, "/wrong.flac");
	library_checkpoint_queue_at(11, 2, false);
	pthread_mutex_unlock(&db_lock);
	assert(library_checkpoint_flush());
	assert(scalar("SELECT pos FROM PLAYBACK_QUEUE_STATE") == 2);
}

int main(int argc, char **argv) {
	assert(argc == 2);
	setup();
	if (!strcmp(argv[1], "coalesce"))
		coalesce();
	else if (!strcmp(argv[1], "epoch"))
		epoch_case();
	else if (!strcmp(argv[1], "generation"))
		generation_case();
	else if (!strcmp(argv[1], "transaction"))
		transaction_case();
	else if (!strcmp(argv[1], "refusal"))
		refusal_case();
	else if (!strcmp(argv[1], "close"))
		close_case();
	else if (!strcmp(argv[1], "queue-entry"))
		queue_entry_case();
	else
		assert(!"unknown case");
	library_checkpoint_shutdown();
	assert(sqlite3_close(db) == SQLITE_OK);
	db = NULL;
	puts("PASS real SQLite bounded checkpoints, ownership and durability");
	return 0;
}
