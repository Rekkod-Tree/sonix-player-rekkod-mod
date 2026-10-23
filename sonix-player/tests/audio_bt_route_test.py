"""Actual shared PCM open/setup with scripted opens and real ALSA null/config."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parent.parent)
p.add_argument('--reviewed-overlay', type=Path)
p.add_argument('--results', type=Path)
p.add_argument('--baseline-audio', type=Path, help='Optional explicit old audio.c for unchanged non-BT/DoP actual traces')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--negative', action='store_true')
a = p.parse_args()
root = a.source_root.resolve()
overlay = a.reviewed_overlay.resolve() if a.reviewed_overlay else root
source_path = overlay / 'src/system/audio/audio.c'
fixture = Path(__file__).with_suffix('.c')
pins = {str(q): hashlib.sha256(q.read_bytes()).hexdigest() for q in (source_path, fixture)}
source = source_path.read_text(encoding='utf-8')
if a.baseline_audio:
    pins[str(a.baseline_audio)] = hashlib.sha256(a.baseline_audio.read_bytes()).hexdigest()


def function(text, name):
    m = list(re.finditer(r'^(?:static )?(?:const )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+' + name + r'\([^;]*?\)\s*\{', text, re.M))[-1]
    end, depth = m.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end] + '\n'


def change(text, name, old, new):
    f = function(text, name)
    assert f.count(old) == 1, (name, old)
    return text.replace(f, f.replace(old, new), 1)


variants = [('actual', source, None)]
if a.negative:
    variants += [
        ('one-time-route', source.replace('bool retarget = route.bluetooth && !pcm_exact_rate;', 'bool retarget = false && route.bluetooth && !pcm_exact_rate;'), 'lost-default'),
        ('unconditional-clear', change(source, 'output_device_set_if', 'if (expected && strcmp(output_pcm, expected) != 0)', 'if (false && expected && strcmp(output_pcm, expected) != 0)'), 'cas-race'),
        ('accept-stale-success', change(source, 'open_pcm_device_now', 'if (err >= 0 && !route_changed)', 'if (err >= 0)'), 'stale-success'),
        ('renew-budget', change(source, 'open_pcm_device_now', 'route_changed = retarget && pcm_open_route_refresh(&route, sample_rate, follow_track_rate);', 'route_changed = retarget && pcm_open_route_refresh(&route, sample_rate, follow_track_rate); if (route_changed) attempts = 0;'), 'churn'),
        ('retarget-dop', source.replace('bool retarget = route.bluetooth && !pcm_exact_rate;', 'bool retarget = route.bluetooth;'), 'bt-dop'),
        ('reuse-converted-identity', change(source, 'pcm_device_name', 'pcm && pcm == bt_converted_pcm && name && strcmp(name, BT_CONVERTED_PCM) == 0', 'pcm && pcm == bt_converted_pcm'), 'converted-reuse'),
        ('old-fallback-classification', change(source, 'open_pcm_device_now', 'bool bluetooth = route.bluetooth && !used_default;', 'bool bluetooth = route.bluetooth;'), 'fallback'),
        ('skip-late-validation', source.replace('// Configuration can also outlive a route change. Do not publish that PCM.\n\tif (retarget &&', '// Configuration can also outlive a route change. Do not publish that PCM.\n\tif (false && retarget &&'), 'late-success'),
        ('nominal-sleep-time', source.replace('(unsigned long long)(paused_clock_ms() - began)', '(unsigned long long)(attempts * 50 + (began - began))'), 'grace'),
        ('no-hw-setup-retry', source.replace('if (retarget && bluetooth && (err == -EBUSY || err == -EAGAIN))', 'if (false && retarget && bluetooth && (err == -EBUSY || err == -EAGAIN))'), 'hw-grace'),
        ('keep-failed-setup-handle', source.replace('failure->code = err;\n\t\tsnd_pcm_close(pcm_handle);\n\t\tpcm_handle = NULL;', 'failure->code = err;\n\t\tpcm_handle = NULL;'), 'hw-grace'),
        ('wrong-failed-endpoint', source.replace('failure->bt_ordinary_endpoint = retarget && bluetooth;', 'failure->bt_ordinary_endpoint = retarget;'), 'hw-lost-default'),
        ('expired-fallback-goto', change(source, 'open_pcm_device_now', 'err = -EAGAIN;\n\t\t\tif (attempts < budget && paused_clock_ms() < deadline)', 'err = -EAGAIN;\n\t\t\tif (attempts < budget)'), 'deadline-fallback'),
        ('expired-route-goto', change(source, 'open_pcm_device_now', 'if (err < 0 && retarget && pcm_open_route_refresh(&route, sample_rate, follow_track_rate)) {\n\t\troute_changed = true;\n\t\tif (attempts < budget && paused_clock_ms() < deadline)', 'if (err < 0 && retarget && pcm_open_route_refresh(&route, sample_rate, follow_track_rate)) {\n\t\troute_changed = true;\n\t\tif (attempts < budget)'), 'deadline-churn'),
        ('reset-hw-budget', source.replace('uint64_t now = paused_clock_ms();\n\t\t\tif (attempts < budget', 'uint64_t now = paused_clock_ms(); deadline = now + 8000; attempts = 0;\n\t\t\tif (attempts < budget'), 'hw-never'),
        ('fallback-S16-on-busy', source.replace('} else if (!pcm_handle && dev_bits == 32 && !passthrough && pcm_setup_fallback_allowed(&open_error))', '} else if (!pcm_handle && dev_bits == 32 && !passthrough)'), 'hw-grace'),
    ]
if a.baseline_audio:
    variants.append(('baseline-compatible', a.baseline_audio.read_text(encoding='utf-8'), None))
    variants.append(('legacy-error-slot', a.baseline_audio.read_text(encoding='utf-8'), 'interleaved-errors'))
cases = ('lost-default', 'to-b', 'to-dac', 'grace', 'generation', 'stale-success', 'stale-generation',
         'converted-reuse', 'late-success', 'fallback-race', 'fallback', 'fallback-fails', 'churn', 'cas-race', 'inactive', 'non-bt', 'usb-dop', 'bt-dop', 'hw-grace', 'hw-eagain', 'hw-generation', 'hw-never', 'hw-open-budget', 'hw-hard-format', 'hw-nomem', 'hw-non-bt', 'hw-dop', 'deadline-churn', 'interleaved-errors', 'hw-lost-default', 'hw-lost-default-eagain', 'deadline-fallback')
names = ('log_ms', 'paused_clock_ms', 'output_device_set_if', 'audio_set_output_device', 'audio_get_output_device',
         'pcm_device_name', 'pcm_takes_s24', 'open_bluetooth_at_rate', 'pcm_open_route_refresh', 'pcm_setup_fallback_allowed', 'open_pcm_device_now', 'open_pcm_device')
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
actual_outputs = {}
with tempfile.TemporaryDirectory(prefix='audio-bt65-') as temporary:
    temporary = Path(temporary)
    logs = a.results.parent / (a.results.stem + '-logs') if a.results else temporary / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    for name, text, negative_case in variants:
        assert negative_case is None or text != source
        macros = ('AUDIO_DEFAULT_PCM', 'BT_CONVERTED_PCM', 'PERIOD_TARGET_MS', 'PERIOD_FRAMES_MAX',
                  'PERIOD_FRAMES_MIN', 'USB_BUFFER_MAX_BYTES', 'PCM_NOMEM_HALVINGS')
        globals_text = ''.join(re.search(r'^#define ' + macro + r' .+$', text, re.M)[0] + '\n' for macro in macros)
        for variable in ('output_pcm', 'output_lock', 'pcm_exact_rate', 'pcm_forced_buffer_ms', 'pcm_device_open',
                         'last_open_err', 'bt_converted_pcm', 'bt_converted_device', 'pcm_opening', 'quiet_since_ms'):
            declaration = re.search(r'^static [^\n;]*\b' + variable + r'\b[^\n;]*;', text, re.M)
            if not declaration and variable == 'last_open_err':
                continue
            assert declaration, variable
            globals_text += declaration[0] + '\n'
        if '} pcm_open_error_t;' in text:
            end = text.index('} pcm_open_error_t;') + len('} pcm_open_error_t;')
            start = text.rfind('typedef struct {', 0, end)
            globals_text += text[start:end] + '\n#define HAS_PCM_ERROR_CONTEXT 1\n'
        if '} pcm_open_route_t;' in text:
            end = text.index('} pcm_open_route_t;') + len('} pcm_open_route_t;')
            start = text.rfind('typedef struct {', 0, end)
            globals_text += text[start:end] + '\n'
        if 'static bool pcm_setup_fallback_allowed(' in text:
            globals_text += '#define HAS_FORMAT_CLASSIFIER 1\n'
            for macro, function_name, prefix in (
                ('ACTUAL_S16_FALLBACK', 'play_decoded_file', '} else if (!pcm_handle && dev_bits == 32 && !passthrough'),
                ('ACTUAL_RESAMPLED_FALLBACK', 'play_decoded_file', 'if (!pcm_handle && resampler.on'),
                ('ACTUAL_EXTERNAL_FALLBACK', 'external_open_locked', 'if (!external_pcm && external_conv.on')):
                f = function(text, function_name)
                start = f.index('(', f.index(prefix)); end = start + 1; depth = 1
                while depth:
                    depth += (f[end] == '(') - (f[end] == ')'); end += 1
                globals_text += '#define ' + macro + ' ' + f[start:end] + '\n'
        (temporary / 'actual_route_globals.inc').write_text(globals_text, encoding='utf-8', newline='\n')
        selected_names = tuple(n for n in names if re.search(r'^(?:static )?(?:const )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+' + n + r'\([^;]*?\)\s*\{', text, re.M))
        (temporary / 'actual_route_functions.inc').write_text(''.join(function(text, n) for n in selected_names), encoding='utf-8', newline='\n')
        binary = temporary / name
        flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-I' + str(temporary), '-I' + str(root)]
        if a.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        built = subprocess.run([*flags, str(fixture), '-lasound', '-pthread', '-o', str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=90)
        (logs / (name + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8', newline='\n')
        assert built.returncode == 0, (name, built.stderr)
        for case in (('non-bt', 'usb-dop', 'bt-dop', 'hw-lost-default', 'hw-lost-default-eagain') if name == 'baseline-compatible' else (negative_case,) if negative_case else cases):
            ran = subprocess.run([str(binary), case], capture_output=True, text=True, encoding='utf-8', timeout=20,
                                 env=dict(os.environ, TMPDIR=str(temporary), ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
            (logs / (name + '-' + case + '.log')).write_text(ran.stdout + ran.stderr, encoding='utf-8', newline='\n')
            if negative_case:
                assert ran.returncode < 0 and 'Assertion' in ran.stderr, (name, case, ran.returncode, ran.stdout, ran.stderr)
            else:
                assert ran.returncode == 0, (name, case, ran.returncode, ran.stdout, ran.stderr)
            if name == 'actual':
                actual_outputs[case] = ran.stdout
            elif name == 'baseline-compatible':
                assert ran.stdout == actual_outputs[case], (case, ran.stdout, actual_outputs[case])
            records.append({'variant': name, 'case': case, 'negative': bool(negative_case), 'compiled': True,
                            'returncode': ran.returncode, 'passed': True})
            print('PASS', name, case, flush=True)
assert pins == {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in pins}
if a.results:
    a.results.write_text(json.dumps({'passed': True, 'sanitized': a.sanitize, 'pins': pins, 'records': records,
                                    'actual_ALSA_null_backend': True, 'device_executed': False}, indent=2) + '\n', encoding='utf-8', newline='\n')
