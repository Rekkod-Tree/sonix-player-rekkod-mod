"""Run the production external-PCM recovery functions against inert ALSA doubles.

From sonix-player: python3 tests/audio_external_recovery_test.py
Use --sanitize for ASan/UBSan and --negative for bounded regression controls.
No ALSA device, network, card, configuration, or firmware operation is used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile


parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative', action='store_true')
parser.add_argument('--implementation', type=Path)
parser.add_argument('--results', type=Path)
args = parser.parse_args()
folder = Path(__file__).resolve().parent
repo = folder.parent
candidate = (args.implementation or repo / 'src/system/audio/audio.c').resolve()
source_bytes = candidate.read_bytes()
text = source_bytes.decode()
source_hash = hashlib.sha256(source_bytes).hexdigest()


def function_range(source, name):
    match = re.search(r'^[ \t]*(?!//|/\*|\*)[^;\n]+\b' + name +
                      r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return match.start(), end


def function(source, name):
    start, end = function_range(source, name)
    return source[start:end] + '\n'


def mutate_function(source, name, before, after):
    start, end = function_range(source, name)
    body = source[start:end]
    assert body.count(before) == 1, (name, before)
    return source[:start] + body.replace(before, after) + source[end:]


functions = ('external_write_cancelled', 'external_reopen_pcm_locked',
             'external_wait_room', 'audio_external_begin_latency',
             'audio_external_write', 'audio_external_end')
for name in functions:
    function(text, name)
begin = function(text, 'audio_external_begin_latency')
assert begin.count('if (external_write_cancelled())') == 3
write = function(text, 'audio_external_write')
assert 'uint64_t stalled = paused_clock_ms() - last_progress_ms;' in write
assert 'if (external_write_cancelled())' in write
assert 'if (++recoveries > 1)' in write
reopen = function(text, 'external_reopen_pcm_locked')
assert 'external_open_locked()' not in reopen
assert 'open_pcm_device(external_channels, external_dev_rate, external_dev_bits' in reopen

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-fsanitize=undefined', '-fno-omit-frame-pointer',
         '-fno-pie', '-no-pie',
         '-pthread']
if args.sanitize:
    flags[flags.index('-fsanitize=undefined')] = '-fsanitize=address,undefined'
env = dict(os.environ)
if args.sanitize:
    env['ASAN_OPTIONS'] = 'detect_leaks=1:abort_on_error=1'
records = []

with tempfile.TemporaryDirectory(prefix='sonix-external-recovery-') as temporary:
    temporary = Path(temporary)

    def run_fixture(name, implementation, negative=None):
        directory = temporary / name
        directory.mkdir()
        extracted = directory / 'external_functions.inc'
        extracted.write_text(''.join(function(implementation, item) for item in functions))
        binary = directory / name
        command = flags + ['-DEXTERNAL_FUNCTIONS_IMPLEMENTATION=' + json.dumps(str(extracted)),
                           str(folder / 'audio_external_recovery_test.c'), '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        try:
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    timeout=2 if negative else 10, env=env)
            failed = result.returncode != 0
            evidence = result.stderr[-1600:] if failed else result.stdout[-600:]
            if negative == 'timeout':
                assert not failed, evidence
                raise AssertionError(f'{name}: expected the bounded timeout to expose the regression')
            if failed and negative is None:
                print(result.stdout)
                print(result.stderr)
            assert (not failed) if negative is None else failed, (name, evidence)
            if negative == 'assertion':
                assert result.returncode < 0 and 'Assertion' in result.stderr, (name, evidence)
        except subprocess.TimeoutExpired:
            assert negative == 'timeout', f'{name}: positive fixture timed out'
            evidence = 'bounded subprocess timeout exposed the unbounded recovery/cancellation loop'
        records.append({'name': name, 'passed': negative is None,
                        'negative_control_rejected': negative is not None,
                        'evidence': evidence.strip()})
        print(('REJECTED ' if negative else 'PASS ') + name)

    run_fixture('external-recovery', text)
    if args.negative:
        unbounded = mutate_function(text, 'audio_external_write',
                                    'if (stalled >= BT_WRITE_STALL_MS) {',
                                    'if (false && stalled >= BT_WRITE_STALL_MS) {')
        run_fixture('unbounded-no-progress-negative', unbounded, 'timeout')

        reset_converter = mutate_function(text, 'external_reopen_pcm_locked',
                                          '\tsnd_pcm_drop(external_pcm);',
                                          '\tbt_resample_close(&external_conv);\n\tsnd_pcm_drop(external_pcm);')
        run_fixture('converter-reset-negative', reset_converter, 'assertion')

        begin_start, begin_end = function_range(text, 'audio_external_begin_latency')
        begin_body = text[begin_start:begin_end]
        waited_end_check = '\tif (external_write_cancelled()) {\n\t\tpthread_mutex_unlock(&external_lock);\n\t\treturn false;\n\t}\n'
        assert begin_body.count(waited_end_check) == 2
        # Remove only the recheck immediately after external_wait_closed().
        offset = begin_body.index('external_wait_closed(EXTERNAL_YIELD_WAIT_MS);')
        check_at = begin_body.index(waited_end_check, offset)
        begin_without_wait_check = (text[:begin_start] + begin_body[:check_at] +
                                    begin_body[check_at + len(waited_end_check):] + text[begin_end:])
        run_fixture('missing-begin-wait-cancel-negative', begin_without_wait_check, 'assertion')

        start, end = function_range(text, 'audio_external_write')
        body = text[start:end]
        cancel_check = '\t\tif (external_write_cancelled())\n\t\t\tbreak;\n'
        assert body.count(cancel_check) == 2
        no_cancel = text[:start] + body.replace(cancel_check, '') + text[end:]
        run_fixture('removed-cancel-negative', no_cancel, 'timeout')

assert hashlib.sha256(candidate.read_bytes()).hexdigest() == source_hash, 'Production source changed during checks'
if args.results:
    args.results.write_text(json.dumps({
        'candidate_sha256': source_hash,
        'fixture_sha256': hashlib.sha256((folder / 'audio_external_recovery_test.c').read_bytes()).hexdigest(),
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'records': records,
    }, indent=2) + '\n')
print('External PCM recovery checks complete; production source and device were not changed.')
