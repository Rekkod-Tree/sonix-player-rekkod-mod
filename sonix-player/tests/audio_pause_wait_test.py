"""Run actual pause helpers/public commands with deterministic eventfd/ALSA fixtures.

From sonix-player: python3 tests/audio_pause_wait_test.py
Use --lvgl-root for an existing build tree when this checkout has no lvgl folder.
No audio device, network, card, configuration or firmware operation is performed.
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
parser.add_argument('--controls', action='store_true')
parser.add_argument('--repo', type=Path)
parser.add_argument('--implementation', type=Path)
parser.add_argument('--lvgl-root', type=Path)
parser.add_argument('--results', type=Path)
args = parser.parse_args()
folder = Path(__file__).resolve().parent
repo = (args.repo or folder.parent).resolve()
candidate = (args.implementation or repo / 'src/system/audio/audio.c').resolve()
lvgl = (args.lvgl_root or repo).resolve()
assert (lvgl / 'lvgl/lvgl.h').is_file(), 'Supply --lvgl-root pointing to an existing matching build tree.'
text = candidate.read_text()
assert text.count('paused_release_due(&pcm_handle, paused_at_ms);') == 2
assert text.count('paused_wait(pcm_handle != NULL, paused_at_ms);') == 2
assert text.count('audio_command == AUDIO_CMD_STOP || play_request || stop_thread') == 2


def function_range(source, name):
    match = re.search(r'^(?:static )?[^;\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
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


resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-ffunction-sections', '-fdata-sections', '-DLV_CONF_INCLUDE_SIMPLE=1',
         '-I' + str(repo), '-I' + str(repo / 'src/system/audio'),
         '-I' + str(lvgl), '-I' + str(lvgl / 'lvgl')]
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
env = dict(os.environ)
if args.sanitize:
    env['ASAN_OPTIONS'] = 'detect_leaks=1:abort_on_error=1'
records = []
with tempfile.TemporaryDirectory(prefix='sonix-pause13-') as temporary:
    temporary = Path(temporary)

    def run_fixture(name, implementation, negative=False):
        directory = temporary / name
        directory.mkdir()
        source = directory / 'audio.c'
        source.write_text(implementation)
        producer = directory / 'producer.inc'
        producer.write_text(function(implementation, 'audio_external_begin_yielding'))
        binary = directory / name
        command = flags + ['-DAUDIO_IMPLEMENTATION=' + json.dumps(str(source)),
                           '-DPAUSE_PRODUCER_IMPLEMENTATION=' + json.dumps(str(producer)),
                           str(folder / 'audio_pause_wait_test.c'),
                           '-Wl,--gc-sections', '-Wl,--wrap=poll', '-Wl,--wrap=__poll_chk', '-Wl,--wrap=clock_gettime',
                           '-Wl,--wrap=usleep', '-Wl,--wrap=read', '-Wl,--wrap=__read_chk',
                           '-lasound', '-pthread', '-lm', '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        try:
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5, env=env)
            failed = result.returncode != 0
            evidence = result.stderr[-1200:] if failed else result.stdout[-500:]
            if failed and not negative:
                print(result.stdout)
                print(result.stderr)
        except subprocess.TimeoutExpired:
            assert negative, 'Positive pause fixture timed out'
            failed, evidence = True, 'Bounded negative control timeout: notification was lost'
        assert failed == negative, (name, evidence)
        records.append({'name': name, 'passed': not negative, 'broken_variant_rejected': negative,
                        'evidence': evidence.strip()})
        print(('REJECTED ' if negative else 'PASS ') + name)

    run_fixture('pause13', text)
    if args.negative:
        variants = [
            ('missing-normal-resume-wake', mutate_function(text, 'audio_resume', '\tplayback_wake();', '')),
            ('sticky-hold-flush', mutate_function(text, 'paused_release_due', '\t\t\thold_flush_request = false;', '')),
            ('early-hold-ack', mutate_function(text, 'paused_release_due', '\tbool flush = hold_flush_request;',
                                             '\tbool flush = hold_flush_request;\n\thold_flush_request = false;')),
            ('fallback-spins', mutate_function(text, 'paused_wait', '\t\tusleep(local_pcm_open ? 50000 : 200000);',
                                              '\t\t(void)local_pcm_open;')),
            ('truncated-pause-clock', mutate_function(text, 'paused_clock_ms',
                '\treturn (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;',
                '\treturn (uint32_t)((uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U);')),
            ('external-owner-treated-as-local', mutate_function(text, 'paused_wait',
                '(local_pcm_open && atomic_load(&paused_release_wanted))',
                '(pcm_device_open && atomic_load(&paused_release_wanted))')),
            ('missing-external-yield-wake', mutate_function(text, 'audio_external_begin_yielding',
                '\t\tplayback_wake();', '')),
        ]
        for name, implementation in variants:
            run_fixture(name, implementation, True)
    if args.controls:
        for name in ('audio_commands_test.c', 'audio_progress_observation_test.c'):
            source = temporary / name
            source.write_text((repo / 'tests' / name).read_text().replace(
                '#include "src/system/audio/audio.c"', '#include ' + json.dumps(str(candidate))))
            binary = source.with_suffix('')
            # Preserve each existing fixture's documented warning policy.
            control_flags = [item for item in flags if item not in ('-Wall', '-Wextra', '-Werror')]
            subprocess.run(control_flags + [str(source), '-Wl,--gc-sections', '-lasound',
                                           '-pthread', '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env=env, timeout=10)
            records.append({'name': name, 'passed': True})
if args.results:
    args.results.write_text(json.dumps({
        'candidate_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
        'test_sha256': hashlib.sha256((folder / 'audio_pause_wait_test.c').read_bytes()).hexdigest(),
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'external_producer_sha256': hashlib.sha256(function(text, 'audio_external_begin_yielding').encode()).hexdigest(),
        'records': records,
    }, indent=2) + '\n')
print('Pause helper/command checks complete; no source or device changes.')
