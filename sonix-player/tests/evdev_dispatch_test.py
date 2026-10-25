"""Real LVGL input dispatch from Linux frames; requires matching built host LVGL objects."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, re, resource, shlex, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-root', type=Path, required=True)
p.add_argument('--results', type=Path)
a = p.parse_args(); root = Path(__file__).resolve().parent.parent
driver = a.host_root / 'lvgl/src/drivers/evdev/lv_evdev.c'
text = driver.read_text()
spec = importlib.util.spec_from_file_location('frame_patch', root / 'tools/lvgl_evdev_frames.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
baseline = text.replace(module.FRAME, '')
candidate = module.patched(baseline)
main = (root / 'src/main.c').read_text()
m = re.search(r'^static void tap_guard_cb\([^;]+?\{', main, re.M); assert m
end, depth = m.end(), 1
while depth:
    depth += (main[end] == '{') - (main[end] == '}'); end += 1
guard = main[m.start():end]
objects = sorted((a.host_root / 'build_host/lvgl').rglob('*.o')); assert len(objects) > 100
resource.setrlimit(resource.RLIMIT_CORE, (0, 0)); records = []
with tempfile.TemporaryDirectory(prefix='sonix-evdev-dispatch-') as d:
    d = Path(d); inc = d / 'guard.inc'; inc.write_text(guard)
    for name, source in [('baseline', baseline), ('actual', candidate)]:
        impl = d / (name + '.c'); impl.write_text(source)
        binary = d / name
        flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections',
                 '-DHOST_BUILD=1', '-DLV_CONF_INCLUDE_SIMPLE=1', '-I' + str(root), '-I' + str(a.host_root),
                 '-I' + str(a.host_root / 'lvgl'), '-I' + str(driver.parent), '-I/usr/include/freetype2', '-I/usr/include/SDL2',
                 '-DEVDEV_IMPLEMENTATION=' + json.dumps(str(impl)), '-DTAP_GUARD_IMPLEMENTATION=' + json.dumps(str(inc))]
        libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'freetype2', 'sdl2'], text=True))
        result = subprocess.run(flags + [str(root / 'tests/evdev_dispatch_test.c'), *map(str, objects), '-Wl,--gc-sections', *libs, '-pthread', '-lm', '-ldl', '-o', str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=12)
        assert (result.returncode != 0) == (name == 'baseline'), result.stdout + result.stderr
        if name == 'baseline': assert 'clicks == 1' in result.stderr
        records.append(dict(name=name, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        print(('REJECTED ' if name == 'baseline' else 'PASS ') + name)
assert driver.read_text() == text
if a.results:
    a.results.write_text(json.dumps(dict(passed=True, real_LVGL_dispatch_tested=True, records=records,
        driver_sha256=hashlib.sha256(candidate.encode()).hexdigest(), tap_guard_sha256=hashlib.sha256(guard.encode()).hexdigest(),
        host_objects={str(p.relative_to(a.host_root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}), indent=2) + '\n')
