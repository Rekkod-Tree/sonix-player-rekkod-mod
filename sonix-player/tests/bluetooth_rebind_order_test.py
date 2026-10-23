"""Execute the actual rebind/order and reconciliation code with inert peers."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--results', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = (root / 'src/system/bluetooth/bluetooth.c').read_text(encoding='utf-8')
start = source.index('static void recover_bus(bool enabled, bool bluez_lost) {')
end = source.index('\n}\n', start) + 3
recovery = source[start:end]
start = source.index('static void publish_bus_loss(bool enabled) {')
end = source.index('\n}\n', start) + 3
recovery = source[start:end] + recovery
match = re.search(r'static bool grace_expired\(void\)\s*\{[^{}]*\}', source)
assert match, 'Expected the existing pure grace-deadline helper'
grace = match.group()
start = source.index('static bool device_still_connected(')
end = source.index('\n}\n', start) + 3
connected = source[start:end]
start = source.index('\tchar mac[BT_MAC_MAX];', source.index('static void refresh_audio_state(void)'))
end = source.index('\tbool rebuilt =', start)
reconcile = source[start:end]
harness = r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "src/system/bluetooth/bluetooth.h"
#include "src/system/bluetooth/btstack.h"
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool rebind_waiting_device, g_scanning;
static bt_state_t g_state;
static int g_pending_volume;
static bt_stream_t g_rx_stream;
static unsigned g_rx_serial, g_serial;
static bt_device_t g_paired[2];
static int g_paired_count;
static char trace[32];
static bool can_open, adapter, pcm, bluez_lost, bluez_during_open, bluez_during_adapter;
static uint32_t a2dp_missing_since, clock_now;
#define A2DP_GRACE_MS 3000
static uint32_t now_ms(void) { return clock_now; }
static void note(char ch) { size_t n = strlen(trace); trace[n] = ch; trace[n + 1] = 0; }
void btstack_close(void) { note('C'); }
bool btstack_bluez_rebind_needed(void) { return bluez_lost; }
static void refresh_devices(void) { note('D'); }
static void refresh_audio_state(void) { note('G'); }
bool btstack_open(void) { note('O'); if (bluez_during_open) bluez_lost = true; return can_open; }
bool btstack_adapter_ready(void) { note('A'); if (bluez_during_adapter) bluez_lost = true; return adapter; }
bool btstack_register_agent(void) { note('R'); return true; }
void btstack_refresh_audio(void) { note('P'); }
bool btstack_audio_sink(char *out, size_t n) { snprintf(out, n, "AA:BB:CC:DD:EE:FF"); return pcm; }
unsigned btstack_sink_serial(void) { return 1; }
''' + recovery + connected + grace + '\nstatic bool eligible_sink(void) {\n' + reconcile + r'''
    (void)sink_serial;
    return sink;
}
static void reset(void) {
    memset(trace, 0, sizeof(trace));
    g_scanning = true;
    g_pending_volume = 73;
    memset(&g_rx_stream, 1, sizeof(g_rx_stream));
    g_rx_serial = g_serial = 0;
    can_open = adapter = pcm = bluez_lost = true;
    bluez_during_open = bluez_during_adapter = false;
}
int main(void) {
    reset(); recover_bus(false, false);
    assert(!strcmp(trace, "CD") && g_state == BT_STATE_OFF && !g_scanning);
    assert(g_pending_volume == -1 && g_rx_serial == 1 && g_serial == 1);
    bt_stream_t empty = {0}; assert(!memcmp(&g_rx_stream, &empty, sizeof(empty)));
    reset(); can_open = false; recover_bus(true, false);
    assert(!strcmp(trace, "CDO") && g_state == BT_STATE_ON);
    reset(); adapter = false; recover_bus(true, false);
    assert(!strcmp(trace, "CDOA"));
    reset(); recover_bus(true, false);
    assert(!strcmp(trace, "CDOARPD"));

    reset(); bluez_lost = false; recover_bus(true, false);
    assert(!strcmp(trace, "COARPD")); // no temporary jack route during codec restart
    reset(); bluez_lost = false; can_open = false; recover_bus(true, false);
    assert(!strcmp(trace, "COG"));
    reset(); bluez_lost = false; adapter = false; recover_bus(true, false);
    assert(!strcmp(trace, "COAG"));
    reset(); bluez_lost = false; recover_bus(false, false);
    assert(!strcmp(trace, "CD"));

    reset(); bluez_lost = false; can_open = false; bluez_during_open = true; recover_bus(true, false);
    assert(!strcmp(trace, "COD"));
    reset(); bluez_lost = false; adapter = false; bluez_during_adapter = true; recover_bus(true, false);
    assert(!strcmp(trace, "COAD"));

    reset(); bluez_lost = false; recover_bus(true, true);
    assert(!strcmp(trace, "CDOARPD")); // preserve the cause across an in-job reopen

    g_paired_count = 1;
    snprintf(g_paired[0].mac, sizeof(g_paired[0].mac), "AA:BB:CC:DD:EE:FF");
    g_paired[0].connected = false;
    rebind_waiting_device = true;
    assert(!eligible_sink() && rebind_waiting_device);
    g_paired[0].connected = true;
    assert(eligible_sink() && !rebind_waiting_device);
    g_paired[0].connected = false;
    assert(eligible_sink()); // unchanged ordinary codec-grace admission
    pcm = false; rebind_waiting_device = true;
    assert(!eligible_sink() && rebind_waiting_device);
    a2dp_missing_since = 0; clock_now = 50000; assert(!grace_expired());
    a2dp_missing_since = 100; clock_now = 3099; assert(!grace_expired());
    clock_now = 3100; assert(grace_expired());
    a2dp_missing_since = UINT32_MAX - 1000; clock_now = 1998; assert(!grace_expired());
    clock_now = 1999; assert(grace_expired());
    puts("Rebind order/reconciliation: PASS 20 cases");
    return 0;
}
'''
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='sonix-rebind-order-') as temporary:
    test = Path(temporary) / 'test.c'
    test.write_text(harness, encoding='utf-8')
    binary = Path(temporary) / 'test'
    command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I' + str(root), str(test), '-pthread', '-o', str(binary)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    build = subprocess.run(command, capture_output=True)
    assert build.returncode == 0, build.stderr.decode(errors='replace')
    result = subprocess.run([str(binary)], capture_output=True,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1'))
    receipt = {'passed': result.returncode == 0, 'sanitize': args.sanitize,
               'returncode': result.returncode, 'stdout': result.stdout.decode(errors='replace'),
               'stderr': result.stderr.decode(errors='replace'),
               'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
               'executed_fragments_sha256': hashlib.sha256((recovery + connected + grace + reconcile).encode()).hexdigest(),
               'scope': 'Actual code fragments with inert peers; not a device/whole-worker acceptance',
               'device_or_socket_used': False}
    if args.results:
        assert not args.results.exists(), 'Keep prior evidence'
        args.results.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(receipt['stdout'], end='')
    assert receipt['passed'], receipt['stderr']
