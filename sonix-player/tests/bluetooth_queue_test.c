// Generated actual-function headers come from bluetooth_queue_test.py.
// Backend/config/AVRCP seams are inert; no radio, device or files are touched.
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BT_MAC_MAX 20
#define BT_CODEC_MAX 24
#define BT_NAME_MAX 64
typedef enum { BT_OP_IDLE, BT_OP_BUSY, BT_OP_OK, BT_OP_FAILED } bt_op_t;
enum { BT_STATE_OFF };
#include "bluetooth_queue_jobs.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static job_t queue[JOB_QUEUE_LEN];
static int queue_head, queue_count;
static bool worker_running = true, g_enabled, g_busy, g_scanning, g_a2dp_connected;
static int g_state, g_found_count, g_pending_volume = -1;
static unsigned g_serial, saves, config_sets, writes;
static int written[8];
static bt_op_t g_op;
static char g_op_name[BT_NAME_MAX];
static bool inject_on_save;
static void post_job(const job_t *job);

static void config_set_int(const char *section, const char *key, long value) {
	(void)section;
	(void)key;
	(void)value;
	config_sets++;
	if (inject_on_save) {
		inject_on_save = false;
		for (int i = 0; i < JOB_QUEUE_LEN; i++) {
			job_t job = {.type = JOB_CODECS};
			post_job(&job);
		}
	}
}
static bool config_save(void) { saves++; return true; }
static void do_set_volume(int value) { assert(writes < 8); written[writes++] = value; }

#include "bluetooth_queue_actual.h"

static void saturated(void) {
	while (queue_count < JOB_QUEUE_LEN) {
		job_t job = {.type = JOB_CODECS};
		post_job(&job);
	}
}

int main(int argc, char **argv) {
	assert(argc == 2);
	const char *test = argv[1];
	job_t job;
	g_enabled = true;
	if (!strcmp(test, "scan-full")) {
		saturated(); bluetooth_scan_start(); assert(!g_scanning && queue_count == JOB_QUEUE_LEN);
	} else if (!strcmp(test, "operation-full")) {
		saturated(); post_device_job(JOB_PAIR, "00:00:00:00:00:01"); assert(g_op == BT_OP_FAILED);
	} else if (!strcmp(test, "codec-auto-full")) {
		saturated(); bluetooth_set_codec_auto(); assert(g_op == BT_OP_FAILED);
	} else if (!strcmp(test, "codec-explicit-full")) {
		saturated(); bluetooth_set_codec("LDAC"); assert(g_op == BT_OP_FAILED);
	} else if (!strcmp(test, "power-on-full")) {
		g_enabled = false; saturated(); set_enabled(true, true);
		assert(!g_enabled && !g_busy && !config_sets && !saves);
	} else if (!strcmp(test, "power-off-race")) {
		saturated(); inject_on_save = true; set_enabled(false, true);
		bool found = false;
		while (take_job(&job)) found |= job.type == JOB_POWER;
		assert(found && !g_enabled && g_busy && config_sets == 1 && saves == 1);
	} else if (!strcmp(test, "volume-deferred")) {
		g_a2dp_connected = true; saturated(); bluetooth_notify_volume(23); bluetooth_notify_volume(40);
		while (take_job(&job)) fixture_process(job);
		bluetooth_notify_volume(60); while (take_job(&job)) fixture_process(job);
		assert(writes == 2 && written[0] == 40 && written[1] == 60 && g_pending_volume == -1);
	} else if (!strcmp(test, "normal")) {
		bluetooth_scan_start(); assert(g_scanning && take_job(&job) && job.type == JOB_SCAN);
		g_scanning = false; post_device_job(JOB_PAIR, "00:00:00:00:00:01");
		assert(g_op == BT_OP_BUSY && take_job(&job) && job.type == JOB_PAIR);
	} else if (!strcmp(test, "fifo")) {
		for (int i = 0; i < JOB_QUEUE_LEN; i++) { job_t value = {.type = JOB_CODECS, .arg_int = i}; post_job(&value); }
		for (int i = 0; i < JOB_QUEUE_LEN; i++) assert(take_job(&job) && job.arg_int == i);
		assert(!take_job(&job));
	} else if (!strcmp(test, "refresh-full")) {
		job_t refresh = {.type = JOB_REFRESH}; post_job(&refresh); saturated(); post_job(&refresh);
		int count = 0; while (take_job(&job)) count += job.type == JOB_REFRESH;
		assert(count == 1);
	} else if (!strcmp(test, "off-clears")) {
		g_scanning = true; g_a2dp_connected = true; bluetooth_notify_volume(55); saturated(); set_enabled(false, false);
		assert(queue_count == 1 && !g_scanning && g_pending_volume == -1);
		assert(take_job(&job) && job.type == JOB_POWER && job.arg_int == 0 && !take_job(&job));
	} else if (!strcmp(test, "volume-coalesce")) {
		g_a2dp_connected = true; for (int i = 0; i < 100; i++) bluetooth_notify_volume(i);
		assert(queue_count == 1 && take_job(&job)); fixture_process(job);
		assert(writes == 1 && written[0] == 99 && !take_job(&job));
	} else if (!strcmp(test, "operation-name")) {
		strcpy(g_op_name, "Previous headphones"); post_device_job(JOB_CONNECT, "00:00:00:00:00:02"); assert(!g_op_name[0]);
		queue_count = 0; saturated(); strcpy(g_op_name, "Old codec"); bluetooth_set_codec("LDAC");
		assert(g_op == BT_OP_FAILED && !g_op_name[0]);
	} else if (!strcmp(test, "scan-disabled")) {
		g_enabled = false; bluetooth_scan_start(); assert(!g_scanning && !queue_count);
	} else if (!strcmp(test, "scan-busy")) {
		g_busy = true; bluetooth_scan_start(); assert(!g_scanning && !queue_count);
	} else if (!strcmp(test, "scan-existing")) {
		g_scanning = true; bluetooth_scan_start(); assert(g_scanning && !queue_count);
	} else if (!strcmp(test, "invalid-device")) {
		post_device_job(JOB_PAIR, NULL); post_device_job(JOB_PAIR, ""); assert(!queue_count && g_op == BT_OP_IDLE);
	} else if (!strcmp(test, "invalid-codec")) {
		bluetooth_set_codec(NULL); bluetooth_set_codec(""); assert(!queue_count && g_op == BT_OP_IDLE);
	} else if (!strcmp(test, "power-same")) {
		set_enabled(true, true); assert(!queue_count && !g_busy && !config_sets && !saves);
	} else if (!strcmp(test, "volume-disabled")) {
		g_enabled = false; g_a2dp_connected = true; bluetooth_notify_volume(12);
		assert(!queue_count && g_pending_volume == -1 && !take_job(&job));
	} else if (!strcmp(test, "volume-disconnected")) {
		bluetooth_notify_volume(12); assert(!queue_count && g_pending_volume == -1 && !take_job(&job));
	} else {
		return 2;
	}
	return 0;
}
