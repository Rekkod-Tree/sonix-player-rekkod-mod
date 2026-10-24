from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--reviewed-overlay', type=Path)
parser.add_argument('--lvgl-root', type=Path, required=True)
parser.add_argument('--results', type=Path, required=True)

parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative', action='store_true')
args = parser.parse_args()
F = Path(__file__).resolve().parent
R = args.source_root.resolve()
if (R / 'sonix-player').is_dir(): R /= 'sonix-player'
O = args.reviewed_overlay.resolve() if args.reviewed_overlay else R
if (O / 'sonix-player').is_dir(): O /= 'sonix-player'
H = args.lvgl_root.resolve()
source = (O / 'src/gui/nowplaying/player.c').read_text(encoding='utf-8')
mini = (O / 'src/gui/shell/sx_mini.c').read_text(encoding='utf-8')


def function(text, name):
    match = list(re.finditer(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+' + name + r'\([^;]*?\)\s*\{', text, re.M))[-1]
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


code = ''.join(function(source, name) for name in ('np_mini_take_thumb240', 'np_mini_thumb_refresh', 'np_mini_from_cover', 'player_mini_mode_changed', 'np_thumb240_drop', 'thumb240_arrived', 'np_thumb240_request', 'np_album_tone_now', 'np_album_changed'))
variants = [('actual', code, False)]
if args.negative:
    variants += [
        ('hidden-thumb-read', code.replace('if (!sx_mini_wants_cover()) {', 'if (false && !sx_mini_wants_cover()) {', 1), True),
        ('hidden-scaled-copy', code.replace('!sx_mini_wants_cover() || ', '', 1), True),
        ('missing-mode-detach', code.replace(function(code, 'player_mini_mode_changed'), function(code, 'player_mini_mode_changed').replace('sx_mini_set_cover(NULL, 0);', '(void)0;', 1)), True),
        ('lost-cover-accent-request', code.replace('if (np_album_id && !covertone_for_album', 'if (false && np_album_id && !covertone_for_album', 1), True),
        ('ui-large-cache-read', code.replace('np_thumb240_request();', 'covertone_thumb(np_album_id, COVERTONE_THUMB_LARGE, &mini_thumb_dsc, mini_thumb_buf, sizeof(mini_thumb_buf));', 1), True),
        ('same-album-premature-free', code.replace('np_album_tone_known = np_album_tone_now();', 'sx_artview_release_cover(&mini_cover_copy); np_album_tone_known = np_album_tone_now();', 1), True),
    ]
out = args.results.resolve()
out.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
flags = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections',
         '-I' + str(O), '-I' + str(R), '-I' + str(H), '-I' + str(H / 'lvgl'), '-DLV_CONF_INCLUDE_SIMPLE=1']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
records = []
with tempfile.TemporaryDirectory(prefix='mini67-policy-') as temp:
    t = Path(temp)
    for name, text, negative in variants:
        (t / 'actual_cover_demand.inc').write_text(text, encoding='utf-8', newline='\n')
        binary = t / name
        built = subprocess.run([*flags, '-I' + str(t), str(F / 'mini_player_async_cover_test.c'), '-Wl,--gc-sections', '-o', str(binary)],
                               capture_output=True, text=True, encoding='utf-8', timeout=60)
        (out / (name + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8', newline='\n')
        assert built.returncode == 0, built.stderr
        ran = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=10, env=env)
        (out / (name + '.log')).write_text(ran.stdout + ran.stderr, encoding='utf-8', newline='\n')
        assert (ran.returncode < 0 and 'Assertion' in ran.stderr) if negative else ran.returncode == 0, (name, ran.returncode, ran.stderr)
        records.append({'name': name, 'negative': negative, 'passed': True})
        print('PASS', name, flush=True)
    setter = function(mini, 'sx_mini_set_mode')
    load = re.search(r'\tmini_mode = config_get_int\("ui", "mini_player"[^;]+;', mini)[0]
    config_test = '''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "src/system/core/config.h"
enum { SX_MINI_NORMAL, SX_MINI_COMPACT };
static int mini_mode;
static void *slot;
static void cancel_press(void) {}
static void apply_layout(void) {}
static void player_mini_mode_changed(void) {}
static void sx_frame_slot_changed(void) {}
''' + setter + '\nstatic void load_mode(void) {\n' + load + '''
}
int main(int argc,char **argv) {
 assert(argc==3); int expected = !strcmp(argv[2],"1") ? 1 : 0;
 assert(config_init(argv[1])); load_mode(); assert(mini_mode==expected);
 assert(config_get_int("ui","accent",-2)==-1);
 assert(sx_mini_set_mode(expected)==false);
 if(expected==1) assert(sx_mini_set_mode(0));
 assert(sx_mini_set_mode(1)); assert(config_init(argv[1])); load_mode(); assert(mini_mode==1);
 assert(config_get_int("ui","accent",-2)==-1);
 assert(sx_mini_set_mode(999)); assert(config_init(argv[1])); load_mode(); assert(mini_mode==0);
 assert(config_get_int("ui","accent",-2)==-1);
 puts("PASS actual config persistence/default/clamp and existing accent preservation");
}
'''
    (t / 'config-test.c').write_text(config_test, encoding='utf-8', newline='\n')
    binary = t / 'config-test'
    built = subprocess.run([*flags, str(t / 'config-test.c'), str(R / 'src/system/core/config.c'), '-Wl,--gc-sections', '-pthread', '-o', str(binary)],
                           capture_output=True, text=True, encoding='utf-8', timeout=60)
    (out / 'config-build.log').write_text(built.stdout + built.stderr, encoding='utf-8', newline='\n')
    assert built.returncode == 0, built.stderr
    for value in ('missing', '0', '1', '-1', '999'):
        path = t / ('config-' + value + '.ini')
        path.write_text('[ui]\naccent=-1\n' + ('' if value == 'missing' else 'mini_player=' + value + '\n'), encoding='utf-8', newline='\n')
        ran = subprocess.run([str(binary), str(path), value], capture_output=True, text=True, encoding='utf-8', timeout=10, env=env)
        assert ran.returncode == 0, (value, ran.stderr)
        records.append({'name': 'real-config-' + value, 'passed': True})
        print('PASS config', value, flush=True)
(out / 'results.json').write_text(json.dumps({'passed': True, 'sanitized': args.sanitize,
    'actual_player_mini_functions': True, 'actual_config_module': True, 'records': records,
    'player_sha256': hashlib.sha256((O / 'src/gui/nowplaying/player.c').read_bytes()).hexdigest(),
    'mini_sha256': hashlib.sha256((O / 'src/gui/shell/sx_mini.c').read_bytes()).hexdigest(),
    'production_changed': False, 'device_executed': False}, indent=2) + '\n', encoding='utf-8', newline='\n')
