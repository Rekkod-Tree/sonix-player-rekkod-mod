"""Exercise actual WAV/decoded initial-PCM-open failure blocks without hardware.

python3 tests/audio_output_open_test.py --lvgl-root /path/to/build --sanitize --controls
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
parser.add_argument('--lvgl-root', type=Path)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--controls', action='store_true')
parser.add_argument('--results', type=Path)
args = parser.parse_args()
folder = Path(__file__).resolve().parent
repo = folder.parent
lvgl = (args.lvgl_root or repo).resolve()
assert (lvgl / 'lvgl/lvgl.h').is_file(), 'Supply --lvgl-root for the matching build tree'
source = repo / 'src/system/audio/audio.c'
text = source.read_text()
patterns = {
    'wav': r'\tif \(!pcm_handle\) \{\n\t\tfclose\(f\);.*?\n\t\}',
    'decoded': r'\tif \(!pcm_handle\) \{\n\t\tset_dac_dop\(0\);\n\t\tdecoder_close\(dec\);.*?\n\t\}',
}
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-ffunction-sections', '-fdata-sections', '-I' + str(repo),
         '-I' + str(repo / 'src/system/audio'), '-I' + str(lvgl), '-I' + str(lvgl / 'lvgl')]
env = dict(os.environ)
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    env['ASAN_OPTIONS'] = 'detect_leaks=1:abort_on_error=1'
records = []
with tempfile.TemporaryDirectory(prefix='sonix-output-open-') as temporary:
    temporary = Path(temporary)
    for kind, pattern in patterns.items():
        matches = list(re.finditer(pattern, text, re.S))
        assert len(matches) == 1, (kind, 'Initial-open failure block must be unique')
        body = matches[0].group()
        body = body[body.index('{') + 1:-1]
        assert body.count('retain_failed_output(filepath);') == 1
        variants = [('current', text, body, None)]
        if args.controls:
            variants.append(('old-stop-policy', text,
                             body.replace('retain_failed_output(filepath);',
                                          'audio_command = AUDIO_CMD_STOP; playback_status = AUDIO_STATUS_STOPPED;'),
                             'playback_status == AUDIO_STATUS_PAUSED && restart_fresh_pending'))
            needle = '\trestart_fresh_pending = false; // explicit stop/card removal cancels cold resume'
            assert text.count(needle) == 1
            variants.append(('uncancelled-cold-retry', text.replace(needle, ''), body,
                             '!restart_fresh_pending && playback_status == AUDIO_STATUS_STOPPED'))
        for name, implementation, failure_body, assertion in variants:
            fixture = temporary / (kind + '-' + name)
            fixture.mkdir()
            audio = fixture / 'audio.c'
            audio.write_text(implementation)
            failure = fixture / 'failure.inc'
            failure.write_text(failure_body)
            binary = fixture / 'test'
            command = flags + ['-DAUDIO_IMPLEMENTATION=' + json.dumps(str(audio)),
                               '-DFAILURE_IMPLEMENTATION=' + json.dumps(str(failure))]
            if kind == 'decoded':
                command.append('-DDECODED_FIXTURE=1')
            command += [str(folder / 'audio_output_open_test.c'), '-Wl,--gc-sections',
                        '-lasound', '-pthread', '-lm', '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(binary)], capture_output=True, text=True, env=env, timeout=10)
            if assertion:
                assert result.returncode == -6 and assertion in result.stderr, result.stderr
            else:
                assert result.returncode == 0, result.stdout + result.stderr
            records.append(dict(path=kind, variant=name, returncode=result.returncode,
                                passed=True, negative_control=bool(assertion),
                                evidence=(result.stderr if assertion else result.stdout).strip()[-1400:]))
            print(('REJECTED ' if assertion else 'PASS ') + kind + '/' + name)
if args.results:
    args.results.write_text(json.dumps(dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                                           passed=True, records=records), indent=2) + '\n')
