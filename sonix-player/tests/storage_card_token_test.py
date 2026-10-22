"""Actual system/USB storage lifecycle and legacy/token API regression.
Defaults to the repository containing tests/. Optional reviewed overlays are
read-only. Uses the existing storage_handoff_test.c, private TMPDIR fixtures,
inert mount/device seams, real mutex contention and seven runtime mutants.
No output-baseline path, Git, device or persistent source writes are used.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,shlex,subprocess,tempfile

HERE=Path(__file__).resolve().parent
ROOT=OVERLAY=None


def replace(text,old,new):
    if text.count(old)!=1:raise RuntimeError('Fixture source changed: '+old[:100])
    return text.replace(old,new,1)


def main():
    global ROOT,OVERLAY
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=HERE.parent)
    parser.add_argument('--reviewed-overlay',type=Path)
    parser.add_argument('--lvgl-root',type=Path)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    ROOT=args.source_root.resolve()
    OVERLAY=args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
    lvgl=(args.lvgl_root or Path(os.environ.get('LVGL_ROOT',str(ROOT/'lvgl')))).resolve()
    if not (lvgl/'lvgl.h').is_file():
        parser.error('LVGL headers missing; initialize the submodule or pass --lvgl-root')
    inputs=[OVERLAY/'src/system/device/system.c',OVERLAY/'src/system/device/system.h',
            ROOT/'src/system/device/usb.c',ROOT/'tests/storage_handoff_test.c']
    pins={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    fixture=(ROOT/'tests/storage_handoff_test.c').read_text(encoding='utf-8')
    # libc tmpfile() can ignore TMPDIR. Keep even anonymous observer files
    # inside this case's private directory; the existing fixture owns fclose.
    fixture=replace(fixture,'\tFILE *f = tmpfile();',\
        '\tchar private_path[4096];\n'
        '\tint n=snprintf(private_path,sizeof(private_path),"%s/observer-XXXXXX",fixture_directory);\n'
        '\tassert(n>0&&(size_t)n<sizeof(private_path));\n'
        '\tint private_fd=mkstemp(private_path);assert(private_fd>=0);assert(!unlink(private_path));\n'
        '\tFILE *f=fdopen(private_fd,"w+");')
    prefix='''#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <setjmp.h>
static const char *fixture_directory;
static char fixture_mount_root[4096]="/fixture/card";
static bool host_folder,dead_once;
static int observations,pin_opens;
static jmp_buf hotplug_exit;
static const char *fake_hotplug_event;
static int fake_recv_count;
static void fixture_sync(void);
static int fixture_mkdir(const char*,mode_t);
static int fixture_usleep(useconds_t);
static int fixture_socket(int,int,int);
static int fixture_bind(int,const struct sockaddr*,socklen_t);
static ssize_t fixture_recv(int,void*,size_t,int);
#define sync fixture_sync
#define mkdir fixture_mkdir
#define usleep fixture_usleep
#define socket fixture_socket
#define bind fixture_bind
#define recv fixture_recv
'''
    fixture=replace(fixture,'int main(void) {','int legacy_handoff_main(void) {')
    fixture=replace(fixture,'void device_state_note_storage_gone(void) {}','void device_state_note_storage_gone(void) { assert(!card_epoch_valid); }')
    fixture=fixture.replace('strcmp(path, "/fixture/card") == 0','strcmp(path, fixture_mount_root) == 0')
    fixture=replace(fixture,'return opendir("/tmp");','return opendir(fixture_directory);')
    fixture=replace(fixture,'pin_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);','pin_opens++;\n\t\tpin_fd = open(fixture_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);')
    fixture=fixture.replace('assert(mounted && (flags & O_DIRECTORY));','assert((mounted || host_folder) && (flags & O_DIRECTORY));')
    fixture=replace(fixture,'\tassert(mounted);\n\tprobes++;','\tassert(mounted || host_folder);\n\tprobes++;')
    fixture=replace(fixture,'''if (strcmp(path, fixture_mount_root) == 0 && mounted)
		return opendir(fixture_directory);''','''if (dead_once && mounted) { dead_once=false;errno=EIO;return NULL; }
	if (strcmp(path, fixture_mount_root) == 0 && (mounted || host_folder))
		return opendir(fixture_directory);''')
    old='return text_file(mounted ? "/dev/fixture /fixture/card vfat rw 0 0\\n" : elsewhere ? "/dev/fixture /elsewhere vfat rw 0 0\\n" : "rootfs / ubifs rw 0 0\\n");'
    fixture=replace(fixture,old,'''if (mounted) { char line[8192];snprintf(line,sizeof(line),"/dev/fixture %s vfat rw 0 0\\n",fixture_mount_root);return text_file(line); }
		return text_file(elsewhere ? "/dev/fixture /elsewhere vfat rw 0 0\\n" : "rootfs / ubifs rw 0 0\\n");''')
    fixture=replace(fixture,'\t(void)path;\n\tif (fail_stat)', '\tif (!mounted && !host_folder && strcmp(path,fixture_mount_root)==0) { errno=ENOENT;return -1; }\n\tif (fail_stat)')
    for signature in ('static char *fixture_realpath(const char *p, char *out) {','static FILE *fixture_fopen(const char *path, const char *mode) {','static int fixture_statvfs(const char *path, struct statvfs *st) {','static DIR *fixture_opendir(const char *path) {','static int fixture_open(const char *path, int flags, ...) {','static int fixture_access(const char *path, int mode) {'):
        fixture=replace(fixture,signature,signature+'\n\tobservations++;')
    # A root switch in the actual mount syscall fixture is observable to later
    # /proc/mounts/readability probes; no kernel mount syscall is performed.
    fixture=replace(fixture,'\t(void)b;\n\t(void)c;','\t(void)c;')
    fixture=replace(fixture,'\tmounted = true;\n\treturn 0;\n}\nstatic int fixture_umount','\tif (b && strcmp(b,fixture_mount_root)) snprintf(fixture_mount_root,sizeof(fixture_mount_root),"%s",b);\n\tmounted = true;\n\treturn 0;\n}\nstatic int fixture_umount')
    suffix='''
#undef sync
#undef mkdir
#undef usleep
#undef socket
#undef bind
#undef recv
static void fixture_sync(void) { observations++; }
static int fixture_mkdir(const char *p,mode_t m) { (void)p;(void)m;observations++;return 0; }
static int fixture_usleep(useconds_t n) { (void)n;return 0; }
static int fixture_socket(int d,int t,int p) { assert(d==AF_NETLINK);(void)t;(void)p;return 98765; }
static int fixture_bind(int fd,const struct sockaddr *a,socklen_t n) { (void)a;(void)n;assert(fd==98765);return 0; }
static ssize_t fixture_recv(int fd,void *buf,size_t size,int flags) { (void)flags;assert(fd==98765);if(fake_recv_count++)longjmp(hotplug_exit,1);size_t n=strlen(fake_hotplug_event);assert(n<size);memcpy(buf,fake_hotplug_event,n);return (ssize_t)n; }
void podcastdl_cancel(void) {}
void logging_resume_after_usb(void) {}
const char *logging_path(void) { return NULL; }
'''
    actual=(OVERLAY/'src/system/device/system.c').read_text(encoding='utf-8')
    start=actual.index('\tg_storage_cfg = cfg->storage_cfg;',actual.index('void system_start_services('))
    end=actual.index('\n\tpthread_t supervisor;',start)
    startup=actual[start:end]
    if 'card_epoch_initialized = true;' not in startup or 'pthread_mutex_lock(&card_lock);' not in startup:raise RuntimeError('Actual startup ownership block missing')
    addition=(HERE/'storage_card_token_test.c.in').read_text(encoding='utf-8').replace('@ACTUAL_STARTUP_STORAGE_BLOCK@',startup)
    source=prefix+fixture+suffix+addition
    results=[]
    with tempfile.TemporaryDirectory(prefix='managed-storage-token-') as temp:
        temp=Path(temp);file=temp/'test.c';file.write_text(source,encoding='utf-8',newline='\n');binary=temp/'test'
        flags=shlex.split(args.cc)+['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-DHOST_BUILD','-DLV_CONF_INCLUDE_SIMPLE=1','-ffunction-sections','-fdata-sections','-I'+str(OVERLAY),'-I'+str(ROOT),'-I'+str(lvgl.parent),'-I'+str(lvgl)]
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        env={**os.environ,'TMPDIR':str(temp)}
        subprocess.run(flags+[str(file),'-Wl,--gc-sections','-pthread','-o',str(binary)],check=True,cwd=temp,env=env,timeout=60)
        folder=temp/'card';folder.mkdir()
        run=subprocess.run([str(binary),str(folder)],capture_output=True,text=True,encoding='utf-8',errors='replace',cwd=temp,env=env,timeout=25)
        if run.returncode:raise RuntimeError('Actual manager fixture failed '+str(run.returncode)+' '+run.stderr)
        results.append({'case':'actual-lifecycle-and-legacy','passed':True,'stdout':run.stdout,'stderr':run.stderr})
        mutants=[]
        pincheck='else if (!card_epoch_valid || expected->epoch != card_epoch)'
        if actual.count(pincheck)!=1:raise RuntimeError('Conditional pin seam changed')
        mutants.append(('ignore-expected-epoch',actual.replace(pincheck,'else if (!card_epoch_valid)',1)))
        rootswap='''if (strcmp(sd_root, mount_point) != 0 || sd_requires_mount != storage_cfg->manage_mount)
		storage_card_revoke_locked();'''
        if actual.count(rootswap)!=1:raise RuntimeError('Root swap seam changed')
        mutants.append(('root-swap-keeps-old-epoch',actual.replace(rootswap,'(void)mount_point;',1)))
        detach='''static void card_databases_detach(void) {
	storage_card_revoke_locked();
	if (!card_attached) {
		return;
	}'''
        after='''static void card_databases_detach(void) {
	if (!card_attached) {
		return;
	}
	storage_card_revoke_locked();'''
        if actual.count(detach)!=1:raise RuntimeError('Detach seam changed')
        mutants.append(('detach-revoke-after-early-return',actual.replace(detach,after,1)))
        claim='\tcard_update_claimed = false; // Late update cancel must not undo shutdown.'
        if actual.count(claim)!=1:raise RuntimeError('Shutdown update claim seam changed')
        mutants.append(('shutdown-can-be-cancelled-as-update',actual.replace(claim,'',1)))
        busy='''if (pthread_mutex_trylock(&card_lock) != 0) {
		errno = EAGAIN;
		return -1;
	}'''
        if actual.count(busy)!=1:raise RuntimeError('Busy check seam changed')
        mutants.append(('busy-check-returns-current',actual.replace(busy,busy.replace('return -1;','return 0;'),1)))
        mutants.append(('epoch-wrap-reuses-zero',actual.replace('if (card_epoch_valid || card_epoch == UINT64_MAX)','if (card_epoch_valid)',1)))
        legacy='''static int storage_pin_card_locked(void) {'''
        mutants.append(('legacy-pin-needs-new-token',actual.replace(legacy,legacy+'\n\tif (!card_epoch_initialized || !card_epoch_valid) { errno = ESTALE; return -1; }',1)))
        for label,mutant in mutants:
            if mutant==actual:raise RuntimeError('Unchanged negative '+label)
            mutant_root=temp/label;device=mutant_root/'src/system/device';device.mkdir(parents=True)
            (device/'system.c').write_text(mutant,encoding='utf-8',newline='\n')
            (device/'system.h').write_bytes((OVERLAY/'src/system/device/system.h').read_bytes())
            executable=temp/label/'test';card=mutant_root/'card';card.mkdir()
            # Put the mutant overlay before the accepted overlay include path.
            command=flags.copy();command.insert(command.index('-I'+str(OVERLAY)),'-I'+str(mutant_root))
            subprocess.run(command+[str(file),'-Wl,--gc-sections','-pthread','-o',str(executable)],check=True,cwd=temp,env=env,timeout=60)
            rejected=subprocess.run([str(executable),str(card)],capture_output=True,text=True,encoding='utf-8',errors='replace',cwd=temp,env=env,timeout=25)
            if rejected.returncode>=0 or 'Assertion' not in rejected.stderr:raise RuntimeError('Negative not caught at runtime '+label+' '+rejected.stderr[-1000:])
            results.append({'case':label,'passed':True,'runtime_assertion':True,'returncode':rejected.returncode,'assertion':rejected.stderr[-1200:]})
    assert pins=={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}, 'Read-only source inputs changed during execution'
    result={'sanitized':args.sanitize,'results':results,'source_hashes':pins,'production_mutated':False,'device_calls':False}
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
