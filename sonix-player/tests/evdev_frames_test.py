"""Exercise actual LVGL evdev reads with queued Linux input_event frames.

python3 tests/evdev_frames_test.py --lvgl-root /path/to/matching/build --results result.json
No device access; source patch applied to a private copy, never to the SDK.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, resource, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--lvgl-root', type=Path, required=True)
p.add_argument('--results', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
driver = a.lvgl_root / 'lvgl/src/drivers/evdev/lv_evdev.c'
source = driver.read_text()
spec = importlib.util.spec_from_file_location('frame_patch', root / 'tools/lvgl_evdev_frames.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
baseline = source.replace(module.FRAME, '')
candidate = module.patched(baseline)
assert module.patched(candidate) == candidate
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
with tempfile.TemporaryDirectory(prefix='sonix-evdev-frames-') as tmp:
    tmp = Path(tmp)
    # Relative includes resolve through a driver-directory include path, while
    # the modified C lives solely in this temporary test directory.
    for name, text, sanitize in [('baseline', baseline, False), ('actual', candidate, False), ('sanitized', candidate, True)]:
        impl = tmp / (name + '.c'); impl.write_text(text)
        binary = tmp / name
        flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections',
                 '-DLV_CONF_INCLUDE_SIMPLE=1', '-I' + str(root), '-I' + str(a.lvgl_root), '-I' + str(a.lvgl_root / 'lvgl'),
                 '-I' + str(driver.parent), '-DEVDEV_IMPLEMENTATION=' + json.dumps(str(impl))]
        if sanitize: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(flags + [str(root / 'tests/evdev_frames_test.c'), '-Wl,--gc-sections', '-o', str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=8,
                                env=dict(os.environ, ASAN_OPTIONS='abort_on_error=1:detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1'))
        assert (result.returncode != 0) == (name == 'baseline'), result.stdout + result.stderr
        assert 'ERROR: AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr
        if name == 'baseline': assert 'd.state == state' in result.stderr
        records.append(dict(name=name, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        print(('REJECTED ' if name == 'baseline' else 'PASS ') + name)
assert driver.read_text() == source
if a.results:
    a.results.write_text(json.dumps(dict(passed=True, driver_before_sha256=hashlib.sha256(source.encode()).hexdigest(),
                                       driver_after_sha256=hashlib.sha256(candidate.encode()).hexdigest(),
                                       SDK_unchanged=True, real_LVGL_dispatch_tested=False, records=records), indent=2) + '\n')
