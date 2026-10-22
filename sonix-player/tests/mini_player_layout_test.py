from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--reviewed-overlay', type=Path)
parser.add_argument('--lvgl-root', type=Path, required=True)
parser.add_argument('--results', type=Path, required=True)

parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--pilot', action='store_true')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--baseline-mini', type=Path)
parser.add_argument('--font-dir', type=Path, required=True)
parser.add_argument('--negative', action='store_true')
args = parser.parse_args()
F = Path(__file__).resolve().parent
ROOT = args.source_root.resolve()
if (ROOT / 'sonix-player').is_dir(): ROOT /= 'sonix-player'
O = args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
if (O / 'sonix-player').is_dir(): O /= 'sonix-player'
HOST = args.lvgl_root.resolve()
OUT = args.results.resolve()
OUT.mkdir(parents=True, exist_ok=True)
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
objects = sorted((HOST / 'build_host/lvgl').rglob('*.o'))
assert objects
object_pins = {str(p): digest(p) for p in objects}
(OUT / 'readonly-lvgl-objects.json').write_text(json.dumps(object_pins, indent=2) + '\n', encoding='utf-8', newline='\n')
test_pins = {str(p): digest(p) for p in (F/'mini_player_layout_test.c', Path(__file__).resolve())}
support = ('src/gui/shell/sx_frame.c', 'src/gui/shell/settingsrow.c', 'src/gui/shell/sx_card.c',
           'src/gui/shell/sx_styles.c', 'src/gui/shell/sx_palette.c', 'src/gui/shell/theme.c',
           'src/gui/shell/icons.c', 'src/system/image/png_write.c', 'src/gui/settings/appearance.c')
headers = ('src/gui/fonts/fonts.h', 'src/gui/shell/sx_mini.h', 'src/gui/shell/sx_frame.h',
           'src/gui/shell/sx_tokens.h', 'src/gui/shell/settingsrow.h', 'src/gui/shell/sx_card.h',
           'src/gui/shell/sx_styles.h', 'src/gui/shell/sx_palette.h', 'src/gui/shell/theme.h',
           'src/gui/shell/icons.h', 'src/gui/shell/artview.h', 'src/gui/settings/appearance.h',
           'src/gui/nowplaying/player.h', 'src/system/core/config.h',
           'src/system/playback/device_state.h', 'lv_conf.h')
source_files = [(O / p if (O / p).is_file() else ROOT / p) for p in (*support, *headers, 'src/gui/shell/sx_mini.c')]
source_files += [args.font_dir.resolve() / name for name in ('ui-regular.ttf', 'ui-semibold.ttf')]
if args.baseline:
    if args.baseline_mini is None: parser.error('--baseline requires --baseline-mini')
    source_files.append(args.baseline_mini.resolve())
source_pins = {str(p): digest(p) for p in source_files}
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
with tempfile.TemporaryDirectory(prefix='sonix-mini67-') as tmp:
    tmp = Path(tmp)
    mini_source = args.baseline_mini.resolve() if args.baseline else O / 'src/gui/shell/sx_mini.c'
    compiled_mini_pin = digest(mini_source)
    (tmp / 'actual_mini.c').write_bytes(mini_source.read_bytes())
    fonts = re.findall(r'extern lv_font_t (font_ui_\w+);', (ROOT / 'src/gui/fonts/fonts.h').read_text(encoding='utf-8'))
    (tmp / 'actual_fonts.inc').write_text('lv_font_t ' + ','.join(fonts) + ';\n', encoding='utf-8', newline='\n')
    definitions = []
    for i, font in enumerate(fonts):
        size = int(re.search(r'\d+', font)[0])
        file = 'ui-semibold.ttf' if 'bold' in font else 'ui-regular.ttf'
        definitions += [f'snprintf(path,sizeof(path),"%s/{file}",getenv("SONIX_TEST_FONT_DIR"));',
                        f'const lv_font_t *f{i}=lv_freetype_font_create(path,LV_FREETYPE_FONT_RENDER_MODE_BITMAP,{size},LV_FREETYPE_FONT_STYLE_NORMAL);',
                        f'assert(f{i});{font}=*f{i};']
    (tmp / 'load_actual_fonts.inc').write_text('\n'.join(definitions) + '\n', encoding='utf-8', newline='\n')
    flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections',
             '-DHOST_BUILD=1', '-DLV_CONF_INCLUDE_SIMPLE=1', '-I' + str(tmp), '-I' + str(O), '-I' + str(ROOT),
             '-I' + str(HOST), '-I' + str(HOST / 'lvgl'), '-I' + str(ROOT / 'src/gui/shell'),
             '-I/usr/include/freetype2', '-I/usr/include/SDL2']
    link = ['-Wl,--gc-sections', '-Wl,--wrap=lv_malloc', '-Wl,--wrap=lv_realloc']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie']
        link += ['-fsanitize=address,undefined', '-no-pie']
    if args.baseline:
        flags += ['-DBASELINE_MINI=1']
    compiled = []
    for relative in (*support, 'fixture'):
        path = F / 'mini_player_layout_test.c' if relative == 'fixture' else (O / relative if (O / relative).exists() else ROOT / relative)
        obj = tmp / (path.stem + '.o')
        command = ['cc', *flags, '-I' + str(ROOT / Path(relative).parent), '-c', str(path), '-o', str(obj)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=90)
        (OUT / (path.stem + '-build.log')).write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
        assert result.returncode == 0, (path, result.stderr)
        compiled.append(obj)
    libs = shlex.split(subprocess.check_output(['sdl2-config', '--libs'], text=True, encoding='utf-8'))
    libs += ['-lpthread', '-lm', '-ldl', '-lasound']
    libs += shlex.split(subprocess.check_output(['pkg-config', '--libs', 'freetype2'], text=True, encoding='utf-8'))
    binary = OUT / 'mini67-test'
    result = subprocess.run(['g++', '-o', str(binary), *map(str, compiled + objects), *link, *libs],
                            capture_output=True, text=True, encoding='utf-8', timeout=90)
    (OUT / 'link.log').write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
    assert result.returncode == 0, result.stderr
    cases = [(800, -100, 'dark')] if args.pilot else [(h, mode, theme) for h in (800, 720) for mode in (-100, 1, 99) for theme in ('dark', 'light')]
    if args.baseline:
        cases = [(h, -100, theme) for h in (800, 720) for theme in ('dark', 'light')]
    for height, mode, theme in cases:
        dest = OUT / f'{height}-{mode}-{theme}'
        dest.mkdir(exist_ok=True)
        environment = dict(os.environ, SONIX_TEST_FONT_DIR=str(args.font_dir.resolve()),
                           ASAN_OPTIONS='detect_leaks=0:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        result = subprocess.run([str(binary), str(height), str(mode), str(dest), theme], capture_output=True,
                                text=True, encoding='utf-8', timeout=60, env=environment)
        (dest / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
        assert result.returncode == 0, (height, mode, theme, result.stdout, result.stderr)
        records.append({'height': height, 'initial_mode': mode, 'theme': theme, 'passed': True})
        print('PASS actual LVGL', height, mode, theme, flush=True)
    if args.negative:
        assert not args.baseline
        original = (O / 'src/gui/shell/sx_mini.c').read_text(encoding='utf-8')

        def mutate(old, new):
            assert original.count(old) == 1, old
            return original.replace(old, new, 1)

        variants = [
            ('wrong-compact-reserve', mutate('return mini_mode == SX_MINI_COMPACT ? SX_MINI_COMPACT_SLOT_H : SX_MINI_SLOT_H;', 'return SX_MINI_SLOT_H;')),
            ('lost-pointer-capture', mutate('lv_obj_set_press_lock(slot, true);', 'lv_obj_set_press_lock(slot, false);')),
            ('pause-outside-reserve', mutate('(SX_MINI_COMPACT_SLOT_H - SX_MINI_COMPACT_TARGET) / 2 - SX_MINI_MARGIN_TOP);', '(SX_MINI_COMPACT_SLOT_H - SX_MINI_COMPACT_TARGET) / 2);')),
            ('progress-not-last-child', mutate('lv_obj_move_to_index(progress_track, -1);', '(void)0;')),
            ('identical-title-reallocated', mutate('if (title_changed)\n\t\tlv_label_set_text(title, title_text);', 'lv_label_set_text(title, title_text);')),
        ]
        start = original.index('bool sx_mini_set_mode(')
        end = original.index('\nstatic bool screen_hides_mini', start)
        setter = original[start:end]
        assert setter.count('sx_frame_slot_changed();') == 1
        variants.append(('followers-not-notified', original[:start] + setter.replace('sx_frame_slot_changed();', '(void)0;') + original[end:]))
        for name, text in variants:
            (tmp / 'actual_mini.c').write_text(text, encoding='utf-8', newline='\n')
            obj = tmp / (name + '.o')
            built = subprocess.run(['cc', *flags, '-c', str(F / 'mini_player_layout_test.c'), '-o', str(obj)],
                                   capture_output=True, text=True, encoding='utf-8', timeout=90)
            assert built.returncode == 0, (name, built.stderr)
            binary = tmp / name
            linked = subprocess.run(['g++', '-o', str(binary), *map(str, compiled[:-1] + [obj] + objects), *link, *libs],
                                    capture_output=True, text=True, encoding='utf-8', timeout=90)
            assert linked.returncode == 0, (name, linked.stderr)
            dest = OUT / name
            dest.mkdir(exist_ok=True)
            ran = subprocess.run([str(binary), '800', '-100', str(dest), 'dark'], capture_output=True,
                                 text=True, encoding='utf-8', timeout=60, env=environment)
            (dest / 'run.log').write_text(ran.stdout + ran.stderr, encoding='utf-8', newline='\n')
            assert ran.returncode < 0 and 'Assertion' in ran.stderr, (name, ran.returncode, ran.stdout, ran.stderr)
            records.append({'negative_variant': name, 'runtime_assertion_rejected': True})
            print('REJECTED', name, flush=True)
assert test_pins == {str(p): digest(Path(p)) for p in test_pins}
assert compiled_mini_pin == digest(mini_source)
assert source_pins == {str(p): digest(Path(p)) for p in source_pins}
assert object_pins == {str(p): digest(Path(p)) for p in object_pins}
(OUT / 'results.json').write_text(json.dumps({'passed': True, 'source_pins': source_pins,
    'readonly_lvgl_objects_pins_file': str(OUT / 'readonly-lvgl-objects.json'), 'cases': records,
    'actual_mini_frame_settings': True, 'baseline_control': args.baseline, 'compiled_mini_sha256': compiled_mini_pin, 'test_pins': test_pins, 'service_artwork_backends_inert': True, 'sanitized_harness_and_support': args.sanitize,
    'shared_lvgl_objects_sanitized': False, 'process_lifetime_freetype_leak_check_disabled': True,
    'production_changed': False, 'device_executed': False}, indent=2) + '\n', encoding='utf-8', newline='\n')
