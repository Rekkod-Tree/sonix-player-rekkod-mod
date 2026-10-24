"""Exercise actual database bootstrap against the frozen alpha10 schema."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--results', type=Path, required=True)
p.add_argument('--lvgl-root', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.results.mkdir(exist_ok=False)
fixtures = a.results / 'fixtures'
fixtures.mkdir()
fixture = Path(__file__).with_suffix('.c')
files = [fixture, Path(__file__).resolve(), root / 'tests/library_alpha10_schema.inc',
         root / 'tests/playhistory_confirmed_test.c', root / 'src/system/library/library.c',
         root / 'src/system/library/library.h', root / 'src/system/library/covertone.c',
         root / 'src/system/library/covertone.h', root / 'src/system/playback/playhistory.c',
         a.lvgl_root / 'build_host/system/db/sqlite3_impl.o']
pins = {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
binary = a.results / 'history-legacy-upgrade-test'
freetype = subprocess.check_output(['pkg-config', '--cflags', 'freetype2'], text=True).split()
cmd = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
       '-ffunction-sections', '-fdata-sections', '-DHOST_BUILD=1', '-DLV_CONF_INCLUDE_SIMPLE=1',
       '-I'+str(root), '-I'+str(a.lvgl_root), '-I'+str(a.lvgl_root/'lvgl'), *freetype,
       str(fixture), str(root/'src/system/library/covertone.c'),
       str(root/'src/system/playback/playhistory.c'),
       str(a.lvgl_root/'build_host/system/db/sqlite3_impl.o'),
       '-Wl,--gc-sections', '-pthread', '-ldl', '-lm', '-o', str(binary)]
if a.sanitize:
    cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
r = subprocess.run(cmd, text=True, capture_output=True, timeout=120)
(a.results/'build.log').write_text(r.stdout+r.stderr)
assert r.returncode == 0, r.stderr
r = subprocess.run([str(binary), str(fixtures)], text=True, capture_output=True, timeout=90)
(a.results/'run.log').write_text(r.stdout+r.stderr)
assert r.returncode == 0, r.stdout+r.stderr
assert all(hashlib.sha256(Path(n).read_bytes()).hexdigest() == h for n, h in pins.items())
(a.results/'result.json').write_text(json.dumps(
    dict(passed=True, sanitize=a.sanitize, actual_bootstrap=True, sources=pins, command=cmd),
    indent=2)+'\n')
print(r.stdout.strip())
