"""Exercise actual history and library SQLite code without a device."""
import argparse
from pathlib import Path
import subprocess
import json
import hashlib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--results', type=Path, required=True)
p.add_argument('--lvgl-root', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.results.mkdir(parents=True, exist_ok=True)
fixture = Path(__file__).with_suffix('.c')
history = root / 'src/system/playback/playhistory.c'
files = [fixture, history, root / 'src/system/library/library.c', root / 'src/system/library/library.h',
         root / 'src/system/playback/playhistory.h', Path(__file__).resolve(),
         a.lvgl_root / 'build_host/system/db/sqlite3_impl.o']
pins = {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
binary = a.results / 'playhistory-confirmed-test'
command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-ffunction-sections',
		   '-fdata-sections', '-DHOST_BUILD=1', '-DLV_CONF_INCLUDE_SIMPLE=1',
		   '-I' + str(root), '-I' + str(a.lvgl_root), '-I' + str(a.lvgl_root / 'lvgl'), str(fixture), str(history),
		   str(a.lvgl_root / 'build_host/system/db/sqlite3_impl.o'),
		   '-Wl,--gc-sections', '-pthread', '-ldl', '-lm', '-o', str(binary)]
if a.sanitize:
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
built = subprocess.run(command, text=True, capture_output=True, timeout=120)
(a.results / 'build.log').write_text(built.stdout + built.stderr, encoding='utf-8')
assert built.returncode == 0, built.stderr
ran = subprocess.run([str(binary)], text=True, capture_output=True, timeout=60)
(a.results / 'run.log').write_text(ran.stdout + ran.stderr, encoding='utf-8')
assert ran.returncode == 0, ran.stdout + ran.stderr
assert all(hashlib.sha256(Path(name).read_bytes()).hexdigest() == value for name, value in pins.items()), 'Source changed during the test'
(a.results / 'result.json').write_text(json.dumps({'passed': True, 'sanitize': a.sanitize,
    'sources': pins, 'command': command}, indent=2) + '\n', encoding='utf-8')
print(ran.stdout.strip())
