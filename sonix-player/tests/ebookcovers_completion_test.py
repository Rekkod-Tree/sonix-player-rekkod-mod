"""Whole actual module with real pthreads; inert backend and real GUI rings.

Run from sonix-player: python3 tests/ebookcovers_completion_test.py --sanitize
Add --negative for runtime regression controls. No device/Git/old output baseline.
The normal project LVGL headers are required. If they live outside the checkout,
pass their include roots through CFLAGS. --overlay selects an optional review copy.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--overlay', type=Path, help='Optional review root containing src/, sonix-player/src/, or ebookcovers.c')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative', action='store_true')
parser.add_argument('--json', type=Path, help='Optional result file; otherwise no repository files are written')
args = parser.parse_args()
folder = Path(__file__).resolve().parent
repo = args.source_root.resolve()
module_relative = Path('src/gui/ebook/ebookcovers.c')
module = repo / module_relative
if args.overlay:
    choices = [args.overlay / module_relative, args.overlay / 'sonix-player' / module_relative, args.overlay / 'ebookcovers.c']
    found = [path for path in choices if path.is_file()]
    assert found, 'Review overlay has no ebookcovers.c'
    module = found[0]
candidate = module.read_text(encoding='utf-8')
gui = (repo / 'src/gui/shell/gui.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'^bool ' + name + r'\([^;]*?\)\s*\{', gui, re.MULTILINE)
    assert match
    end, depth = match.end(), 1
    while depth:
        depth += (gui[end] == '{') - (gui[end] == '}')
        end += 1
    return gui[match.start():end] + '\n'


bridge = function('gui_post').replace('bool gui_post(', 'static bool actual_gui_post(', 1) + function('gui_post_latest')
cases = ['normal', 'keep', 'full-both', 'idle', 'thread-failure', 'decode-success', 'oom-start-1', 'oom-start-2', 'oom-start-3',
         'oom-decode', 'oom-peek', 'stale-result', 'stop-during-decode', 'stop-waiting',
         'reentrant-replace', 'closing', 'duplicate-wake', 'visible-range', 'stale-stop']
include_flags = ['-I' + str(repo)]
if args.overlay:
    include_flags = ['-I' + str(args.overlay), '-I' + str(args.overlay / 'sonix-player')] + include_flags
flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DHOST_BUILD', '-DLV_CONF_INCLUDE_SIMPLE=1',
         '-ffunction-sections', '-fdata-sections', *include_flags] + shlex.split(os.environ.get('CFLAGS', ''))
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []


def replace_once(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after, 1)


negative = [
    ('drop-rejected-result', replace_once(candidate, '\t\tgui_post(completion_wake, NULL);',
     '\t\tif (!gui_post(completion_wake, NULL)) { pthread_mutex_lock(&job->lock); job->result_ready = false; pthread_mutex_unlock(&job->lock); }'), 'full-both'),
    ('selected-is-delivered', replace_once(candidate, 'job->done[i] = COVERS_IN_FLIGHT;', 'job->done[i] = COVERS_DELIVERED;'), 'normal'),
    ('duplicate-take', replace_once(candidate, '\tjob->result_ready = false;', ''), 'duplicate-wake'),
    ('stop-predicate-lost', replace_once(candidate, '\t\tjob->stopping = true;', '\t\tjob->stopping = false;'), 'stop-waiting'),
    ('ignore-generation', replace_once(candidate, 'if (result.gen == covers_gen && result.cb)', 'if (result.cb)'), 'stale-result'),
    ('ignore-keep', replace_once(candidate, 'if (result.has_cover && !kept)', 'if (result.has_cover && (kept || !kept))'), 'keep'),
    ('ignore-closing', replace_once(candidate, '\tif (completions_closing)\n\t\treturn covers_gen;\n', ''), 'closing'),
    ('late-ownership-transfer', replace_once(replace_once(candidate, '\tjob->done[result.index] = COVERS_DELIVERED;\n', ''),
     '\tif (result.has_cover && !kept)', '\tjob->done[result.index] = COVERS_DELIVERED;\n\tif (result.has_cover && !kept)'), 'reentrant-replace'),
]
if args.negative:
    variants = [(name, text, [case], True) for name, text, case in negative]
else:
    variants = [('production-candidate', candidate, cases, False)]
with tempfile.TemporaryDirectory(prefix='sonix-ebook57-') as temporary:
    temporary = Path(temporary)
    (temporary / 'actual_gui_bridge.h').write_text(bridge, encoding='utf-8', newline='\n')
    for variant, text, scenarios, bad in variants:
        (temporary / 'ebookcovers_under_test.c').write_text(text, encoding='utf-8', newline='\n')
        binary = temporary / variant
        build = subprocess.run(flags + ['-I' + str(temporary), str(folder / 'ebookcovers_completion_test.c'), '-pthread',
                                        '-Wl,--gc-sections', '-o', str(binary)], capture_output=True, text=True)
        assert build.returncode == 0, (variant, build.stderr)
        for case in scenarios:
            run = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=6)
            if bad:
                assert run.returncode != 0 and 'Assertion' in run.stderr, (variant, case, run.returncode, run.stderr)
            else:
                assert run.returncode == 0, (case, run.returncode, run.stdout, run.stderr)
            records.append({'variant': variant, 'case': case, 'passed': True, 'negative_rejected': bad,
                            'exitcode': run.returncode, 'assertion': run.stderr[-1500:] if bad else ''})
            print('REJECTED' if bad else 'PASS', variant, case, flush=True)
report = {'passed': True, 'candidate_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
          'fixture_sha256': hashlib.sha256((folder / 'ebookcovers_completion_test.c').read_bytes()).hexdigest(),
          'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'cases': records, 'flags': flags, 'native': False}
if args.json:
    args.json.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'passed': True, 'cases': len(records), 'negative': args.negative,
                  'candidate_sha256': report['candidate_sha256']}, indent=2))
