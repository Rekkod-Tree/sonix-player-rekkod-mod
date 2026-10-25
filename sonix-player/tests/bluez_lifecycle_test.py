"""Test real BlueZ A2DP ownership functions with GLib and ASan/UBSan.

--source points to patched BlueZ 5.54; --baseline to optional pristine source.
Transport I/O is stubbed. This does not replace complete-daemon/R1 acceptance.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, resource, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--results', type=Path)
a = p.parse_args()
here = Path(__file__).resolve().parent
groups = {
    'callback': (
        ['setup_ref', 'setup_free', 'setup_unref', 'setup_cb_new', 'setup_cb_free',
         'finalize_setup_errno', 'error_to_errno', 'finalize_config', 'a2dp_setup_get', 'a2dp_reconfig'],
        ['single-failure', 'pending-failure', 'abort-failure', 'sender-rejected'],
        {'pending-failure', 'abort-failure', 'sender-rejected'}),
    'timer': (
        ['setup_free', 'setup_unref', 'a2dp_reconfigure', 'setup_reconfigure', 'close_cfm', 'abort_cfm'],
        ['normal', 'freed-before-timer', 'duplicate', 'missing-local', 'missing-remote',
         'missing-nested', 'missing-codec', 'config-error', 'abort-normal',
         'abort-freed-before-timer', 'abort-duplicate', 'mixed-close-abort'],
        {'freed-before-timer', 'duplicate', 'missing-local', 'missing-remote',
         'missing-nested', 'missing-codec', 'abort-freed-before-timer',
         'abort-duplicate', 'mixed-close-abort'}),
}

def function(text, name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;]*?\n\{', text, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1', G_SLICE='always-malloc')
cflags = subprocess.check_output(['pkg-config', '--cflags', 'glib-2.0'], text=True).split()
libs = subprocess.check_output(['pkg-config', '--libs', 'glib-2.0'], text=True).split()
records = []
pins = {}
with tempfile.TemporaryDirectory(prefix='sonix-bluez-lifecycle-') as tmp:
    tmp = Path(tmp)
    for tag, folder in [('candidate', a.source), ('baseline', a.baseline)]:
        if folder is None:
            continue
        path = folder / 'profiles/audio/a2dp.c'
        text = path.read_text()
        pins[tag] = hashlib.sha256(path.read_bytes()).hexdigest()
        for group, (names, cases, known_failures) in groups.items():
            functions = '\n'.join(function(text, n) for n in names
                                  if tag != 'baseline' or n != 'setup_reconfigure')
            template = (here / ('bluez_a2dp_' + group + '_test.c.in')).read_text()
            assert template.count('@PRODUCTION_FUNCTIONS@') == 1
            generated = tmp / (tag + '-' + group + '.c')
            generated.write_text(template.replace('@PRODUCTION_FUNCTIONS@', functions))
            binary = generated.with_suffix('')
            cmd = ['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', *cflags]
            if tag == 'candidate':
                cmd += ['-DFIXED']
            subprocess.run([*cmd, str(generated), *libs, '-o', str(binary)], check=True)
            for case in cases:
                result = subprocess.run([str(binary), case], env=env, capture_output=True,
                                        timeout=10, preexec_fn=no_core)
                failed = tag == 'baseline' and case in known_failures
                assert (result.returncode != 0) == failed, (tag, group, case, result.stderr.decode(errors='replace'))
                if failed:
                    assert b'AddressSanitizer' in result.stderr or b'runtime error:' in result.stderr or result.returncode == -6
                record = dict(version=tag, group=group, case=case,
                              expected_failure=failed, returncode=result.returncode, passed=True)
                records.append(record)
                print(tag, group, case, 'PASS (negative control)' if failed else 'PASS', flush=True)
if a.results:
    a.results.write_text(json.dumps(dict(source_sha256=pins, records=records,
        scope='Extracted real lifecycle/callback/refcount functions; external protocol operations stubbed.'), indent=2) + '\n')
