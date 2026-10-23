"""Real private-bus integration of actual btstack; never uses the system bus."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import resource
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
daemon = shutil.which('dbus-daemon')
assert daemon, 'A private test bus requires dbus-daemon'
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='sonix-bt-owner-') as directory:
    tmp = Path(directory)
    assert tmp.parent == Path('/tmp')
    address = 'unix:path=' + str(tmp / 'bus')
    binary = tmp / 'test'
    compile_command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-ffunction-sections', '-fdata-sections', '-I' + str(root),
                       str(root / 'tests/btstack_owner_bus_test.c'),
                       str(root / 'src/system/bluetooth/btrate.c'),
                       '-pthread', '-Wl,--gc-sections', '-o', str(binary)]
    subprocess.run(compile_command, check=True, capture_output=True)
    config = tmp / 'bus.conf'
    config.write_text('<busconfig><type>system</type><listen>' + address + '</listen>'
                      '<auth>EXTERNAL</auth><policy user="' + str(os.getuid()) + '">'
                      '<allow own="*"/><allow send_destination="*"/>'
                      '<allow receive_sender="*"/></policy></busconfig>', encoding='ascii')
    log = (tmp / 'daemon.log').open('wb')
    server = subprocess.Popen([daemon, '--nofork', '--nopidfile', '--config-file=' + str(config)],
                              stdin=subprocess.DEVNULL, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 4
        while not (tmp / 'bus').exists():
            assert server.poll() is None, 'Private test daemon exited'
            assert time.monotonic() < deadline, 'Private test socket did not appear'
            time.sleep(0.02)
        run = subprocess.run([str(binary), address], capture_output=True, timeout=25)
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3)
        log.close()
    result = {'passed': run.returncode == 0, 'returncode': run.returncode,
              'stdout': run.stdout.decode(errors='replace'), 'stderr': run.stderr.decode(errors='replace'),
              'daemon_finished': server.poll() is not None,
              'private_bus_only': True, 'device_used': False,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'test_source_sha256': hashlib.sha256((root / 'tests/btstack_owner_bus_test.c').read_bytes()).hexdigest(),
              'btstack_sha256': hashlib.sha256((root / 'src/system/bluetooth/btstack.c').read_bytes()).hexdigest()}
    if args.results:
        assert not args.results.exists(), 'Preserve previous evidence'
        args.results.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(result['stdout'], end='')
    assert result['passed'], result['stderr']
