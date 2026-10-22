"""Whole actual btstack/dbuslite fixture, no sockets or device access."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parent.parent)
parser.add_argument('--reviewed-overlay', type=Path)
parser.add_argument('--results', type=Path)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative', action='store_true')
args = parser.parse_args()
root = args.source_root.resolve()
overlay = args.reviewed_overlay.resolve() if args.reviewed_overlay else root
paths = [overlay / 'src/system/bluetooth/btstack.c', overlay / 'tests/btstack_protocol_test.c',
         root / 'src/system/bluetooth/dbuslite.c', root / 'src/system/bluetooth/bapcm.c',
         root / 'src/system/bluetooth/btrate.c']
pins = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
source = paths[0].read_text(encoding='utf-8')
variants = [('actual', source, False)]
if args.negative:
    fixed = 'dbus_call_begin(c, BALSA_SERVICE, BALSA_MANAGER_PATH, OBJMAN_IFACE, "GetManagedObjects", "");'
    assert source.count(fixed) == 1
    variants.append(('old-bluealsa-root', source.replace(fixed, fixed.replace('BALSA_MANAGER_PATH', '"/"')), True))
    bluez = 'dbus_call_begin(c, BLUEZ_SERVICE, "/", OBJMAN_IFACE, "GetManagedObjects", "");'
    assert source.count(bluez) == 3
    variants.append(('bluez-moved-with-bluealsa', source.replace(bluez, bluez.replace('"/"', 'BALSA_MANAGER_PATH')), True))
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
with tempfile.TemporaryDirectory(prefix='bt-discovery64-') as temporary:
    temporary = Path(temporary)
    logs = args.results.parent / (args.results.stem + '-logs') if args.results else temporary / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    staged = temporary / 'src/system/bluetooth'
    staged.mkdir(parents=True)
    for name, text, negative in variants:
        (staged / 'btstack.c').write_text(text, encoding='utf-8', newline='\n')
        binary = temporary / name
        flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-ffunction-sections', '-fdata-sections', '-I' + str(temporary),
                 '-I' + str(overlay), '-I' + str(root), '-I' + str(root / 'src/system/bluetooth')]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        built = subprocess.run([*flags, str(paths[1]), str(paths[4]), '-pthread',
                                '-Wl,--gc-sections', '-o', str(binary)],
                               capture_output=True, text=True, encoding='utf-8', timeout=90)
        (logs / (name + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8', newline='\n')
        assert built.returncode == 0, (name, built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=20, env=env)
        (logs / (name + '.log')).write_text(ran.stdout + ran.stderr, encoding='utf-8', newline='\n')
        if negative:
            assert ran.returncode < 0 and 'Assertion' in ran.stderr, (name, ran.returncode, ran.stdout, ran.stderr)
            expected = '!strcmp(path, "/org/bluealsa")' if name == 'old-bluealsa-root' else '!strcmp(dest, "org.bluez")'
            assert expected in ran.stderr, (name, ran.stderr)
        else:
            assert ran.returncode == 0, (name, ran.returncode, ran.stdout, ran.stderr)
        records.append({'variant': name, 'compiled': True, 'runtime_assertion_rejected': negative,
                        'returncode': ran.returncode, 'passed': True})
        print('PASS', name, flush=True)
assert pins == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
result = {'passed': True, 'sanitized': args.sanitize, 'pins': pins, 'records': records,
          'device_executed': False, 'network_executed': False, 'production_changed': False}
if args.results:
    args.results.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
else:
    print(json.dumps(result, indent=2))
