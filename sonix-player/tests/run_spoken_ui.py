#!/usr/bin/env python3
"""Real host GUI against local normal API fixtures, at both R1 panel heights.

Build the separate host mirror with make host UI_TEST=1 first. Run as the WSL
test user with mount-namespace support. No device, credentials or public API.
Example: python3 tests/run_spoken_ui.py --project /root/sonix-audiobook-dev/sonix-player
    --fixtures /root/sonix-ui-fixtures --output /tmp/spoken-ui-results
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time


def wait_server(port, process):
    for _ in range(50):
        if process.poll() is not None:
            raise RuntimeError('Mock server exited before listening')
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=.2):
                return
        except OSError:
            time.sleep(.1)
    raise RuntimeError('Mock server did not become ready')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    binary = args.project.resolve() / 'sonix_player_host_ui_test'
    assert binary.is_file(), 'Build UI_TEST=1 in the separate host mirror first'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='sonix-spoken-ui-'))
    files = root / 'files'
    files.mkdir()
    for name in ('high.mp3', 'mid.mp3', 'cover.jpg'):
        shutil.copyfile(args.fixtures / name, files / name)
    shutil.copyfile(files / 'high.mp3', files / 't103.mp3')
    # Only public example values, never a developer's local/sealed key file.
    example = configparser.ConfigParser(interpolation=None)
    example.read(source / 'streaming-keys.ini.example', encoding='utf-8')
    keys = configparser.ConfigParser(interpolation=None)
    keys['yandex'] = dict(example['yandex'])
    server_logs, servers = [], []
    try:
        for service, port in (('zvuk', 18769), ('yandex', 18770)):
            log = (output / (service + '-mock.log')).open('w', encoding='utf-8')
            server_logs.append(log)
            process = subprocess.Popen(['python3', str(source / 'tools' / (service + '_mock.py')),
                                        str(files), str(port)], stdout=log, stderr=subprocess.STDOUT)
            servers.append(process)
            wait_server(port, process)
        for height in (800, 720):
            run = root / str(height)
            for part in ('card', 'local', 'tmp', 'app/usr'):
                (run / part).mkdir(parents=True)
            resources = source.parent / 'sonix-packer/assets/R1/usr/resource'
            shutil.copytree(resources, run / 'app/usr/resource',
                            ignore=shutil.ignore_patterns('streaming-keys.bin', 'streaming-keys.ini', 'streaming-keys.key'))
            components = run / 'app/usr/resource/sonix/components'
            components.mkdir(parents=True, exist_ok=True)
            (components / 'streaming-keys.bin').unlink(missing_ok=True)
            with (components / 'streaming-keys.ini').open('w', encoding='utf-8') as file:
                keys.write(file)
            (run / 'config.ini').write_text('''[ui]
language=Russian
[clock]
configured=true
[screen]
statusbar_always=1
[power]
screen_off_seconds=0
[zvuk]
base_url=http://127.0.0.1:18769
[yandex]
base_url=http://127.0.0.1:18770
auth_url=http://127.0.0.1:18770
quality=normal
''', encoding='utf-8')
            with (run / 'config.ini').open('a', encoding='utf-8') as config:
                config.write('[streaming]\nkeys_file=' + str(components / 'streaming-keys.ini') + '\n')
            (run / 'yandex_session.ini').write_text('''access_token=mock-access-token
refresh_token=mock-refresh-token
expires_at=4102444800
uid=12345
user=Test User
''', encoding='utf-8')
            (run / 'alsa.conf').write_text('pcm.!default { type null }\n', encoding='ascii')
            result = output / str(height)
            result.mkdir(exist_ok=True)
            env = dict(os.environ, TESTROOT=str(run), TEST_BINARY=str(binary),
                       SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy',
                       ALSA_CONFIG_PATH=str(run / 'alsa.conf'), SONIX_PANEL=f'480x{height}',
                       SONIX_NO_MOUNT='1', SONIX_NO_SYSSERVER='1', SONIX_SD_ROOT=str(run / 'card'),
                       SONIX_CONFIG=str(run / 'config.ini'), SONIX_UI_TEST_DIR=str(result), SONIX_UI_TEST_SPOKEN='1')
            # The pre-existing native runner uses these mounts too. They are
            # private to this child and disappear when it exits.
            script = '''set -eu
mkdir -p /tmp/.local /mnt/sd_0 /tmp/sonix
mount --bind "$TESTROOT/local" /tmp/.local
mount --bind "$TESTROOT/card" /mnt/sd_0
mount --bind "$TESTROOT/tmp" /tmp/sonix
cd "$TESTROOT/app"
exec "$TEST_BINARY"
'''
            with (result / 'run.log').open('w', encoding='utf-8') as log:
                subprocess.run(['timeout', '-k', '5s', '180s', 'unshare', '-m', '--propagation', 'private',
                                'bash', '-c', script], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            text = (result / 'run.log').read_text(encoding='utf-8', errors='replace')
            assert 'UI_TEST_PASS spoken:' in text, text[-2000:]
            print(f'PASS: spoken UI 480x{height}', flush=True)
    finally:
        for process in servers:
            process.terminate()
        for process in servers:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in server_logs:
            log.close()
    # Looking at the last page must not hydrate the whole 338-episode archive.
    batches = []
    for line in (output / 'zvuk-mock.log').read_text(encoding='utf-8').splitlines():
        prefix = 'mock: GQL getEpisodes '
        if line.startswith(prefix):
            batches.append(json.loads(line[len(prefix):])['ids'])
    assert batches and max(map(len, batches)) <= 50
    hydrated = {str(item) for batch in batches for item in batch}
    assert '100337' in hydrated and '100000' in hydrated and len(hydrated) < 200, len(hydrated)
    print(f'PASS: lazy catalogue hydrated {len(hydrated)} distinct episodes of 338, max batch {max(map(len, batches))}')
    print('Disposable state retained for inspection:', root)


if __name__ == '__main__':
    main()
