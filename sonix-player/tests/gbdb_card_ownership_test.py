"""Actual GBDB/GUI card-ownership regressions; Linux/WSL, cc, LVGL headers.
Defaults to current repository source; optional review overlays are read-only.
No output-baseline path, device, source mutation or persistent test writes.

Storage-owner API outcomes are explicit contract seams here. The separate
core-token actual lifecycle fixture tests that algorithm; this suite tests
retention, original-token propagation and real fd-relative consumer writes.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, resource, shlex, subprocess, tempfile

HERE = Path(__file__).resolve().parent
ROOT = OVERLAY = CORE = None

def function(source, name):
    m = re.search(r'^(?:static )?(?:int|bool|void) ' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    if not m:
        raise RuntimeError(name)
    end, depth = source.index('{', m.start()) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end]

def core_functions(source, gui):
    """Actual owner algorithm with inert mount observer inputs, not a second epoch."""
    state = source[source.index('static uint64_t card_epoch;'):source.index('static void storage_card_revoke_locked(void) {')]
    functions = '\n'.join(function(source, name) for name in (
        'storage_card_revoke_locked', 'storage_card_publish_locked', 'storage_card_owned_locked',
        'storage_card_try_admit', 'storage_card_try_check', 'storage_open_card_locked', 'storage_pin_card_locked', 'storage_pin_card_expected', 'storage_pin_card_if'))
    for name in ('storage_card_try_admit', 'storage_card_try_check', 'storage_pin_card_if'):
        functions = functions.replace(name+'(', 'actual_'+name+'(')
    prefix = '''
#include <sys/statvfs.h>
#include <fcntl.h>
static pthread_mutex_t card_lock=PTHREAD_MUTEX_INITIALIZER;
static char sd_root[1024];
static bool card_released,card_usb_claimed,card_attached=true,sd_writable=true,sd_requires_mount;
static bool usb_storage_active(void) {return false;}
static bool is_mounted(const char *p) {(void)p;return true;}
'''
    root, absent, writable = ('"/fixture"', 'storage_absent', 'true') if gui else ('card', 'no_card', '!read_only')
    suffix = '''
static void change_owner(void) {
 pthread_mutex_lock(&card_lock);storage_card_revoke_locked();
 card_epoch_initialized=true;card_attached=true;snprintf(sd_root,sizeof(sd_root),"%s",@ROOT@);
 storage_card_publish_locked();current_epoch=card_epoch;pthread_mutex_unlock(&card_lock);
}
'''.replace('@ROOT@', root)
    if gui:
        suffix += '''
bool storage_card_try_admit(storage_card_token_t *t,char *r,size_t n) {
 admission_calls++;card_attached=!storage_absent;if(storage_busy)pthread_mutex_lock(&card_lock);
 bool result=actual_storage_card_try_admit(t,r,n);if(storage_busy)pthread_mutex_unlock(&card_lock);return result;
}
int storage_card_try_check(const storage_card_token_t *t) {
 check_calls++;card_attached=!storage_absent;if(storage_busy)pthread_mutex_lock(&card_lock);
 int result=actual_storage_card_try_check(t);if(storage_busy)pthread_mutex_unlock(&card_lock);return result;
}
'''
    else:
        suffix += '''
bool storage_card_try_admit(storage_card_token_t *t,char *r,size_t n) {
 card_attached=!no_card;sd_writable=!read_only;return actual_storage_card_try_admit(t,r,n);
}
int storage_card_try_check(const storage_card_token_t *t) {card_attached=!no_card;return actual_storage_card_try_check(t);}
int storage_pin_card_if(const storage_card_token_t *t) {
 pins++;card_attached=!no_card;sd_writable=!read_only;int fd=track(actual_storage_pin_card_if(t));
 if(fd>=0&&swap_after_pin){swap_after_pin=false;swap_card();}return fd;
}
'''
    return prefix + state + functions + suffix

def main():
    global ROOT, OVERLAY, CORE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=HERE.parent)
    parser.add_argument('--reviewed-overlay', type=Path)
    parser.add_argument('--storage-overlay', type=Path)
    parser.add_argument('--lvgl-root', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative', action='store_true')
    args = parser.parse_args()
    ROOT = args.source_root.resolve()
    OVERLAY = args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
    CORE = args.storage_overlay.resolve() if args.storage_overlay else ROOT
    lvgl = (args.lvgl_root or Path(os.environ.get('LVGL_ROOT', str(ROOT/'lvgl')))).resolve()
    if not (lvgl/'lvgl.h').is_file():
        parser.error('LVGL headers missing; initialize the submodule or pass --lvgl-root')
    pins = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [
        OVERLAY / 'src/system/gearboy/gbdb.c', OVERLAY / 'src/system/gearboy/gbdb.h',
        OVERLAY / 'src/gui/gearboy/gearboypage.c', CORE / 'src/system/device/system.h', CORE / 'src/system/device/system.c']}
    gui_source = (ROOT / 'src/gui/shell/gui.c').read_text(encoding='utf-8')
    module = OVERLAY / 'src/gui/gearboy/gearboypage.c'
    scanner = OVERLAY / 'src/system/gearboy/gbdb.c'
    bridge = function(gui_source, 'gui_post') + '\n' + function(gui_source, 'gui_post_latest')
    gui_fixture = (HERE / 'gearboy_card_ownership_test.c.in').read_text(encoding='utf-8').replace('@ACTUAL_GUI_POST_FUNCTIONS@', bridge)
    writer_fixture = (HERE / 'gbdb_cache_writer_test.c.in').read_text(encoding='utf-8')
    records = []
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    with tempfile.TemporaryDirectory(prefix='gbdb59-consumer-') as tmp:
        tmp = Path(tmp)
        (tmp / 'storage_test_api.inc').write_text('''
bool storage_card_try_admit(storage_card_token_t *out,char *root,size_t size) {
 admission_calls++;memset(out,0,sizeof(*out));root[0]=0;
 if(storage_busy){errno=EAGAIN;return false;}if(storage_absent){errno=ENODEV;return false;}
 assert(size>10);strcpy(root,"/fixture");out->epoch=current_epoch;return true;
}
int storage_card_try_check(const storage_card_token_t *t) {
 check_calls++;if(storage_busy){errno=EAGAIN;return -1;}
 if(storage_absent||!t->epoch||t->epoch!=current_epoch){errno=ESTALE;return -1;}return 0;
}
''', encoding='utf-8', newline='\n')
        flags = shlex.split(args.cc) + [ '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-Wno-misleading-indentation', '-Wno-unused-function', '-ffunction-sections', '-fdata-sections',
                 '-DLV_CONF_INCLUDE_SIMPLE=1', '-I'+str(CORE), '-I'+str(OVERLAY), '-I'+str(ROOT),
                 '-I'+str(lvgl.parent), '-I'+str(lvgl),
                 '-I'+str(ROOT/'src/gui/gearboy'), '-I'+str(OVERLAY/'src/system/gearboy'), '-I'+str(tmp)]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        def run(name, fixture, actual, variants, negative=False):
            file = tmp / (name+'.c')
            file.write_text(fixture.replace('@ACTUAL_GEARBOYPAGE_C@', str(actual)).replace('@ACTUAL_GBDB_C@', str(actual)), encoding='utf-8', newline='\n')
            binary = tmp / name
            subprocess.run(flags+[str(file), '-Wl,--gc-sections', '-pthread', '-lm', '-o', str(binary)], check=True)
            for case, arguments in variants:
                result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, encoding='utf-8', timeout=20,
                                        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                if negative:
                    if result.returncode >= 0 or 'Assertion' not in result.stderr:
                        raise RuntimeError((name, case, result.returncode, result.stdout, result.stderr))
                elif result.returncode:
                    raise RuntimeError((name, case, result.returncode, result.stdout, result.stderr))
                if not negative and name in ('writer', 'actual-core-writer'):
                    published = result.stderr.split('PUBLISHED_EROFS_BEGIN\n', 1)[1].split('PUBLISHED_EROFS_END', 1)[0]
                    quiet_ro = result.stderr.split('PREWRITE_EROFS_BEGIN\n', 1)[1].split('PREWRITE_EROFS_END', 1)[0]
                    assert 'cache published, durability uncertain: Read-only file system' in published, 'post-rename EROFS diagnostic required'
                    assert not quiet_ro, 'ordinary read-only cache refusal must stay quiet'

                records.append(dict(name=name, case=case, passed=not negative, broken_variant_rejected=negative,
                                    stdout=result.stdout, stderr=result.stderr))
                print('REJECTED' if negative else 'PASS', name, case, flush=True)
        run('gui', gui_fixture, module, [('all', []), ('closing-active', ['closing-active']), ('delete-active', ['delete-active'])])
        card = tmp/'writer-card'; card.mkdir()
        run('writer', writer_fixture, scanner, [('all', [str(card)])])
        # The same consumer cases with actual shared epoch/trylock/conditional
        # admission functions, plus real statvfs/open. Lifecycle callback wiring
        # remains the dedicated core-token suite's responsibility.
        core_source = (CORE/'src/system/device/system.c').read_text(encoding='utf-8')
        (tmp/'actual_core_gui.inc').write_text(core_functions(core_source, True), encoding='utf-8', newline='\n')
        integrated_gui = gui_fixture.replace('#include "storage_test_api.inc"', '#include "actual_core_gui.inc"')
        integrated_gui = integrated_gui.replace('current_epoch++;', 'change_owner();').replace('gui_thread=pthread_self();', 'gui_thread=pthread_self();change_owner();')
        run('actual-core-gui', integrated_gui, module, [('all', []), ('closing-active', ['closing-active']), ('delete-active', ['delete-active'])])
        begin, end = writer_fixture.index('bool storage_card_try_admit('), writer_fixture.index('static int fixture_mkdirat(')
        integrated_writer = writer_fixture[:begin] + core_functions(core_source, False) + writer_fixture[end:]
        integrated_writer = integrated_writer.replace('static void swap_card(void)', 'static void change_owner(void);\nstatic void swap_card(void)').replace('current_epoch++;', 'change_owner();')
        card = tmp/'actual-core-card'; card.mkdir()
        run('actual-core-writer', integrated_writer, scanner, [('all', [str(card)])])
        if args.negative:
            src = module.read_text(encoding='utf-8')
            anchor = '\t\tif (errno == EAGAIN)\n\t\t\treturn; // READY and its payload remain owned until a known result.'
            assert src.count(anchor) == 1
            mutations = [('gui-eagain-as-stale', src.replace(anchor, '', 1)),
                         ('gui-stale-row', src.replace('if (storage_card_try_check(&roms_token) != 0)', 'if (false && storage_card_try_check(&roms_token) != 0)', 1))]
            for name, text in mutations:
                actual = tmp/(name+'-module.c'); actual.write_text(text, encoding='utf-8', newline='\n')
                run(name, gui_fixture, actual, [('all', [])], True)
            src = scanner.read_text(encoding='utf-8')
            mutations = [
                ('writer-refreshed-token', src.replace('storage_pin_card_if(token)', '((void)token, storage_pin_card_if(&(storage_card_token_t){.epoch=current_epoch}))', 1)),
                ('writer-follow-symlink', src.replace('O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC', 'O_RDONLY | O_DIRECTORY | O_CLOEXEC', 1)),
                ('writer-ignore-fsync', src.replace('if (fsync(fd) != 0)\n\t\tgoto failed;', '(void)fsync(fd);', 1)),
                ('writer-ignore-close', src.replace('if (closed != 0)\n\t\tgoto failed;', '(void)closed;', 1)),
                ('writer-reanchor-current-root', src.replace('localfd = cache_open_dir(rootfd, ".local");', 'rootfd = fixture_reanchor(rootfd);\n\tlocalfd = cache_open_dir(rootfd, ".local");', 1)),
                ('writer-truncate-collision', src.replace('O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC', 'O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW | O_CLOEXEC', 1)),
            ]
            for name, text in mutations:
                assert text != src
                actual = tmp/(name+'-module.c'); actual.write_text(text, encoding='utf-8', newline='\n')
                card = tmp/(name+'-card'); card.mkdir()
                run(name, writer_fixture, actual, [('all', [str(card)])], True)
    assert pins == {p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in pins}, 'Reviewed inputs changed during tests'
    result = dict(sanitized=args.sanitize, source_hashes=pins, storage_api_contract_seams=True,
                  actual_storage_token_functions=True, full_storage_lifecycle_wiring=False,
                  production_mutated=False, device_used=False, records=records)
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
