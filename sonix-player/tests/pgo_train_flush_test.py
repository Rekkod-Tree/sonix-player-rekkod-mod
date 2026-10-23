"""Check the actual capture module in a disposable fixture.

From sonix-player/: python3 tests/pgo_train_flush_test.py --source-root .
--results /tmp/pgo-flush.json [--sanitize]. Requires POSIX cc and nm.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import resource
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--source-root', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--results', type=Path, required=True)
a = p.parse_args()
source = a.source_root.resolve() / 'src/system/core/pgo_train.c'
before = source.read_bytes()
hash_bytes = lambda b: hashlib.sha256(b).hexdigest()
records = []

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

with tempfile.TemporaryDirectory(prefix='pgo-flush-actual-') as folder:
    root = Path(folder)
    fixed = root / 'actual.c'
    fixed.write_bytes(before)
    text = before.decode('utf-8')
    assert 'extern void __gcov_flush(void);' in text
    assert '\t\t\t__gcov_flush();' in text
    old = root / 'old-dump-test-mutant.c'
    old.write_text(text.replace('__gcov_flush', '__gcov_dump'), encoding='utf-8', newline='\n')
    harness = Path(__file__).with_name('pgo_train_flush_test.c')
    for name, selected, expected in [('fixed', fixed, 0), ('old-dump', old, -6)]:
        exe = root / name
        cmd = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
               '-DSONIX_PROFILE_GENERATE', '-DSONIX_PGO_TRAIN_SOURCE="' + str(selected) + '"',
               str(harness), '-o', str(exe)]
        if a.sanitize:
            cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        built = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
        assert built.returncode == 0, built.stderr
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        run = subprocess.run([str(exe)], capture_output=True, text=True, encoding='utf-8',
                             timeout=5, cwd=root, env=env, preexec_fn=no_core)
        assert run.returncode == expected, (name, run.returncode, run.stdout, run.stderr)
        if expected:
            assert 'unserialized dump entry was called' in run.stderr
        records.append({'case': name, 'compiled': True, 'returncode': run.returncode,
                        'stdout': run.stdout, 'stderr': run.stderr})
    # Actual module entirely disappears when the GENERATE define is absent.
    obj = root / 'non-generate.o'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-c', str(fixed), '-o', str(obj)], check=True)
    names = subprocess.check_output(['nm', str(obj)], text=True, encoding='utf-8')
    assert not names.strip(), names
    records.append({'case': 'non-generate-empty-object', 'passed': True})
assert source.read_bytes() == before, 'Production source changed while testing'
a.results.parent.mkdir(parents=True, exist_ok=True)
a.results.write_text(json.dumps({'passed': True, 'source_sha256': hash_bytes(before),
                                'sanitize': a.sanitize, 'records': records}, indent=2) + '\n',
                     encoding='utf-8', newline='\n')
print('PASS actual training module: fixed, runtime-old-dump rejection, non-GENERATE empty')
