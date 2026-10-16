#!/usr/bin/env python3
"""Run normal in-memory fixtures with an already completed host build.

python3 tests/stream_metadata_jobs_test.py /path/to/host/build /tmp/test-output
No network, audio, GUI startup, or card access. The production sources are
included to exercise the actual private callbacks, not a copied algorithm.
"""
import pathlib
import shlex
import subprocess
import sys

mirror = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)

def variable(name):
    rule = f'fixture_print:; @printf "%s\\n" "$({name})"'
    return shlex.split(subprocess.check_output(
        ['make', '--no-print-directory', '-s', '--eval', rule, 'fixture_print'],
        cwd=mirror, text=True).strip())

objects = variable('HOST_OBJS')
flags = variable('HOST_CFLAGS')
ldflags = variable('HOST_LDFLAGS')
source = pathlib.Path(__file__).with_suffix('.c').resolve()
for service in ('yandex', 'zvuk'):
    binary = out / f'{service}_metadata_jobs_test'
    obj = binary.with_suffix('.o')
    define = ['-DTEST_ZVUK'] if service == 'zvuk' else []
    subprocess.run(['cc', *flags, *define, '-Dmain=__wrap_main', '-c', str(source),
                    '-o', str(obj)], cwd=mirror, check=True)
    kept = [str(mirror / p) for p in objects if not p.endswith(f'/{service}play.o')]
    assert len(kept) == len(objects) - 1
    subprocess.run(['c++', '-o', str(binary), str(obj), *kept,
                    '-Wl,--wrap=main', f'-Wl,--wrap={service}_tracks', *ldflags], cwd=mirror, check=True)
    subprocess.run([str(binary)], check=True)
