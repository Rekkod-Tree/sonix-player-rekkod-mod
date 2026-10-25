#!/usr/bin/env python3
"""Extract and exercise the actual album-expansion branch; no network/card I/O.

Run: python3 tests/yandex_album_expand_test.py --sanitize --output /tmp/ym-expand
The allocator/move guard records and prevents an out-of-bounds move in an old
source, so the negative control cannot corrupt the test process or device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cc', default='cc')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = args.source or root/'src/system/streaming/yandexdl.c'
text = source.read_text()
entry = re.search(r'typedef struct \{\n\s*bool album;.*?\} entry_t;', text, re.S)
assert entry, 'Production entry structure changed'
start = text.index('// An album: its tracks go to the head of the queue, in order.')
end = text.index('\n\t\t} else {', start)
body = text[start:end]
assert body.count('int take =') == 1 and body.count('free(tracks);') == 1
fixture = Path(__file__).with_suffix('.c').read_text()
assert fixture.count('/* PRODUCTION_ENTRY_TYPE */') == 1
assert fixture.count('/* PRODUCTION_EXPANSION */') == 1
generated = fixture.replace('/* PRODUCTION_ENTRY_TYPE */', entry[0])
generated = generated.replace('/* PRODUCTION_EXPANSION */', body)
args.output.mkdir(parents=True, exist_ok=False)
c = args.output/'fixture.c'
c.write_text(generated)
command = [args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
           '-I'+str(root), str(c), '-pthread', '-o', str(args.output/'test')]
if args.sanitize:
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
with (args.output/'compile.log').open('w') as log:
    subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
result = subprocess.run([str(args.output/'test')], capture_output=True, text=True)
(args.output/'run.log').write_text(result.stdout + result.stderr)
receipt = dict(passed=result.returncode == 0, returncode=result.returncode,
               source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
               extracted_branch_sha256=hashlib.sha256(body.encode()).hexdigest(),
               cases=10, sanitized=args.sanitize, command=command)
(args.output/'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(result.stdout, end='')
if result.stderr:
    print(result.stderr, end='')
raise SystemExit(result.returncode)
