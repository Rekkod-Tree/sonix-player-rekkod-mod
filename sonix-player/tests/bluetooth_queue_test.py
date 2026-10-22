"""Actual Bluetooth queue/state functions with inert radio/config seams.

Run from sonix-player: python3 tests/bluetooth_queue_test.py
Optional CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'.
No device, Git, previous source snapshot, output directory or credentials needed.
"""
from pathlib import Path
import os
import re
import resource
import shlex
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]
production = (base / 'src/system/bluetooth/bluetooth.c').read_text(encoding='utf-8')


def function(source, name):
    match = re.search(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+' + name + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def actual(source):
    text = ''.join(function(source, name) for name in (
        'take_job', 'queue_has', 'enqueue_job_locked', 'rejected_job', 'post_job', 'post_op_job',
        'set_enabled', 'bluetooth_scan_start', 'post_device_job', 'bluetooth_set_codec',
        'bluetooth_set_codec_auto', 'bluetooth_notify_volume'))
    start = source.index('case JOB_VOLUME: {')
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return text + 'static void fixture_process(job_t job){switch(job.type){' + source[start:end] + 'default:break;}}\n'


def replace_once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new, 1)


cases = ('scan-full', 'operation-full', 'codec-auto-full', 'codec-explicit-full', 'power-on-full',
         'power-off-race', 'volume-deferred', 'normal', 'fifo', 'refresh-full', 'off-clears',
         'volume-coalesce', 'operation-name', 'scan-disabled', 'scan-busy', 'scan-existing',
         'invalid-device', 'invalid-codec', 'power-same', 'volume-disabled', 'volume-disconnected')
code = actual(production)
fallback = '\t\tif (g_pending_volume >= 0 && g_enabled && g_a2dp_connected) {\n\t\t\t*out = (job_t){.type = JOB_VOLUME};\n\t\t\treturn true;\n\t\t}\n'
negative = [
    ('scan_intent_without_admission', replace_once(code, '\tif (queued)\n\t\tg_scanning = true;', '\tg_scanning = true;'), 'scan-full'),
    ('busy_after_refusal', replace_once(code, 'g_op = queued ? BT_OP_BUSY : BT_OP_FAILED;', 'g_op = BT_OP_BUSY;'), 'operation-full'),
    ('missing_volume_fallback', replace_once(code, fallback, ''), 'volume-deferred'),
    ('missing_off_volume_clear', replace_once(code, '\t\tqueue_head = queue_count = 0;\n\t\tg_pending_volume = -1;', '\t\tqueue_head = queue_count = 0;'), 'off-clears'),
    ('stale_operation_name', replace_once(code, '\tg_op_name[0] = 0;', ''), 'operation-name'),
]
start = production.index('typedef enum {\n\tJOB_NONE')
jobs = production[start:production.index('static pthread_mutex_t lock', start)]
flags = shlex.split(os.environ.get('CC', 'cc')) + ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
flags += shlex.split(os.environ.get('CFLAGS', ''))
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='sonix-bt-queue-') as temp:
    temp = Path(temp)
    (temp / 'bluetooth_queue_jobs.h').write_text(jobs, encoding='utf-8')
    for label, body, scenarios, bad in [('production', code, cases, False)] + [
            (label, body, [case], True) for label, body, case in negative]:
        (temp / 'bluetooth_queue_actual.h').write_text(body, encoding='utf-8')
        binary = temp / label
        build = subprocess.run(flags + ['-I' + str(temp), str(Path(__file__).with_suffix('.c')), '-pthread', '-o', str(binary)], capture_output=True, text=True)
        assert build.returncode == 0, build.stderr
        for case in scenarios:
            run = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=5)
            if bad:
                assert run.returncode != 0 and 'Assertion' in run.stderr, (label, run.returncode, run.stderr)
            else:
                assert run.returncode == 0, (label, case, run.returncode, run.stderr)
                if case in ('volume-deferred', 'volume-coalesce'):
                    assert not run.stderr, 'Retained volume must not be reported as rejected/lost'
print(f'PASS: {len(cases)} actual-production queue controls; {len(negative)} runtime negative controls rejected')
