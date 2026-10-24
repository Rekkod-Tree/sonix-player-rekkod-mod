"""Compile actual player functions; no device, network or decoder execution."""
from pathlib import Path
import argparse
import hashlib
import json
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lvgl-root', type=Path, required=True)
parser.add_argument('--results', type=Path, required=True)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
args.results.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
player = root / 'src/gui/nowplaying/player.c'
source = player.read_text(encoding='utf-8')
parts = []
for signature in ('static void view_update(', 'bool player_mini_seek_valid(',
                  'bool player_mini_seek_begin(', 'bool player_mini_seek_commit('):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    parts.append(source[start:end])
fixture = Path(__file__).with_suffix('.c')
pins = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (player, fixture, Path(__file__).resolve(), root / 'src/gui/nowplaying/player.h')}
with tempfile.TemporaryDirectory(prefix='sonix-mini-seek-') as folder:
    temp = Path(folder)
    (temp / 'actual_mini_seek.inc').write_text('\n'.join(parts), encoding='utf-8')
    binary = args.results / 'mini-seek-guard-test'
    command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-DHOST_BUILD=1', '-DLV_CONF_INCLUDE_SIMPLE=1', '-I' + str(root),
               '-I' + str(args.lvgl_root), '-I' + str(args.lvgl_root / 'lvgl'),
               '-I' + str(temp), str(fixture), '-o', str(binary), '-lm']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    built = subprocess.run(command, text=True, capture_output=True, timeout=120)
    (args.results / 'build.log').write_text(built.stdout + built.stderr, encoding='utf-8')
    assert built.returncode == 0, built.stderr
    ran = subprocess.run([str(binary)], text=True, capture_output=True, timeout=30)
    (args.results / 'run.log').write_text(ran.stdout + ran.stderr, encoding='utf-8')
    assert ran.returncode == 0, ran.stdout + ran.stderr
    for name, expected in pins.items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected
    (args.results / 'result.json').write_text(json.dumps({'passed': True, 'sanitize': args.sanitize,
        'source_sha256': pins, 'command': command}, indent=2) + '\n', encoding='utf-8')
    print(ran.stdout.strip())
