// Actual worker queue/callback, no worker thread, socket or device.
// cc -std=gnu11 -O1 -g -ffunction-sections -fdata-sections -I.
// -I/path/to/player-with-lvgl -I/path/to/player-with-lvgl/lvgl
// -DLV_CONF_INCLUDE_SIMPLE=1 tests/bluetooth_rebind_queue_test.c -pthread
// -Wl,--gc-sections -o /tmp/bluetooth_rebind_queue_test
#include "src/system/bluetooth/bluetooth.c"
#include <assert.h>

static bool owner_lost;
static bool bluez_lost;
bool btstack_rebind_needed(void) { return owner_lost; }
bool btstack_bluez_rebind_needed(void) { return bluez_lost; }

int main(void) {
	job_t item = {.type = JOB_MEDIA};
	g_enabled = true;
	for (int i = 0; i < JOB_QUEUE_LEN; i++)
		assert(enqueue_job_locked(&item));
	owner_lost = true;
	on_bus_change();
	assert(queue_count == JOB_QUEUE_LEN && bus_rebind_pending);
	job_t next;
	assert(take_job(&next) && next.type == JOB_BUS_REBIND && next.arg_int == 0);
	assert(queue_count == JOB_QUEUE_LEN && !bus_rebind_pending);
	for (int i = 0; i < JOB_QUEUE_LEN; i++)
		assert(take_job(&next) && next.type == JOB_MEDIA);
	assert(!take_job(&next));

	owner_lost = false;
	on_bus_change();
	on_bus_change();
	assert(queue_count == 1 && !bus_rebind_pending);
	assert(take_job(&next) && next.type == JOB_REFRESH);
	assert(!take_job(&next));

	g_enabled = false;
	owner_lost = true;
	on_bus_change();
	assert(take_job(&next) && next.type == JOB_BUS_REBIND);
	assert(!bus_rebind_pending && !take_job(&next));
	g_enabled = true;
	owner_lost = bluez_lost = true;
	on_bus_change();
	bluez_lost = false;
	on_bus_change(); // A following BlueALSA event cannot erase the earlier BlueZ cause.
	// The active worker wait reopens before the queued rebind can run.
	owner_lost = bluez_lost = false;
	assert(take_job(&next) && next.type == JOB_BUS_REBIND && next.arg_int == 1);
	assert(!bus_bluez_lost_pending && !take_job(&next));
	owner_lost = true;
	on_bus_change();
	assert(take_job(&next) && next.type == JOB_BUS_REBIND && next.arg_int == 0);
	puts("Bluetooth rebind queue: PASS full-ring priority, ordinary coalescing, disabled drain, retained BlueZ cause");
	return 0;
}
