"""Run actual two-phase resume against slow/failing PCM and command races."""
from pathlib import Path
import argparse, hashlib, json, os, re, resource, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--lvgl-root', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--controls', action='store_true')
p.add_argument('--results', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
source = root / 'src/system/audio/audio.c'
text = source.read_text()
m = re.search(r'^static bool reopen_paused_pcm\([^;]+?\{', text, re.M)
assert m
end, depth = m.end(), 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
helper = text[m.start():end] + '\n'
assert text.count('if (!reopen_paused_pcm(&pcm_handle,') == 2
variants = [('actual', helper)]
if a.controls:
    opening = '\t*pcm = open_pcm_device(channels, rate, bits, period, NULL);'
    assert helper.count(opening) == 1
    variants.append(('locked-driver', helper.replace(opening, '\tpthread_mutex_lock(&audio_mutex);\n' + opening + '\n\tpthread_mutex_unlock(&audio_mutex);')))
    start = helper.index('\tif (!same_output || resume_request_cancelled(serial))')
    finish = helper.index(') {', start) + 1
    variants.append(('stale-open-accepted', helper[:start] + '\t(void)serial; (void)same_output;\n\tif (false)' + helper[finish:]))
    assert helper.count('\t\t\tusleep(150 * 1000);') == 1
    variants.append(('no-cancelled-close-settle', helper.replace('\t\t\tusleep(150 * 1000);', '')))
    variants.append(('ignored-preopen-command', helper.replace('\tif (resume_request_cancelled(serial)) {', '\tif (false) {')))
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
with tempfile.TemporaryDirectory(prefix='sonix-resume-open-') as d:
    d = Path(d)
    for name, body in variants:
        inc = d / (name + '.inc'); inc.write_text(body)
        binary = d / name
        flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections',
                 '-I' + str(root), '-I' + str(root / 'src/system/audio'), '-I' + str(a.lvgl_root), '-I' + str(a.lvgl_root / 'lvgl'),
                 '-DAUDIO_IMPLEMENTATION=' + json.dumps(str(source)), '-DRESUME_IMPLEMENTATION=' + json.dumps(str(inc))]
        if a.sanitize: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(flags + [str(root / 'tests/audio_resume_open_test.c'), '-Wl,--gc-sections', '-lasound', '-lm', '-pthread', '-o', str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=22,
                                env=dict(os.environ, ASAN_OPTIONS='abort_on_error=1:detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1'))
        expected = name != 'actual'
        assert (result.returncode != 0) == expected, result.stdout + result.stderr
        assert 'ERROR: AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr
        if expected: assert 'Assertion' in result.stderr
        records.append(dict(name=name, rejected=expected, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        print(('REJECTED ' if expected else 'PASS ') + name)
if a.results:
    a.results.write_text(json.dumps(dict(passed=True, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), records=records), indent=2) + '\n')
