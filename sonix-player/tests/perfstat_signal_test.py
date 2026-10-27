"""Real perfstat init/handler contracts in isolated Linux child processes.

Run from sonix-player: python3 tests/perfstat_signal_test.py
Use CFLAGS="-fsanitize=undefined -fno-omit-frame-pointer" for sanitizer checks.
Only host children receive signals; no player, launcher, device, config or Git is used.
"""
from pathlib import Path
import os
import re
import resource
import shlex
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]
production = (base / 'src/system/device/perfstat.c').read_text(encoding='utf-8')


def function(source, name):
    match = re.search(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t]+' + name + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


init = function(production, 'perfstat_init')
registration = '\tsignal(SIGUSR2, sigusr2_cb);\n'
assert init.count(registration) == 1
# Negative controls mutate only the in-memory test copy, never the source file.
missing = init.replace(registration, '')
early_return = '\tif (seconds <= 0 || !disp) {\n\t\treturn;\n\t}\n'
assert missing.count(early_return) == 1
late = missing.replace(early_return, early_return + registration)
variants = (('production_init', init), ('missing_init', missing), ('late_init', late))

prefix = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <link.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
typedef struct {int unused;} lv_display_t;
typedef struct {int unused;} lv_timer_t;
typedef struct {int unused;} lv_indev_t;
enum {LV_EVENT_REFR_START,LV_EVENT_RENDER_START,LV_EVENT_REFR_READY,LV_EVENT_FLUSH_START,LV_EVENT_FLUSH_FINISH,LV_EVENT_ALL,LV_INDEV_TYPE_POINTER};
static bool enabled,log_areas,log_phases;
static lv_display_t *display;
static uint32_t report_ms;
static uintptr_t load_bias;
static uint64_t last_wall_us;
static unsigned long long last_proc_ticks;
static volatile sig_atomic_t report_now,usr1_seen;
static int event_count,timer_count,ignore_count,thread_baselines;
static long config_seconds;
static lv_timer_t report_timer;
static long config_get_int(const char *section,const char *key,long fallback) {
 (void)fallback;assert(!strcmp(section,"debug")&&!strcmp(key,"perf_stats"));return config_seconds;
}
static void refr_event_cb(void *event) {(void)event;}
static void input_event_cb(void *event) {(void)event;}
static lv_indev_t *lv_indev_get_next(lv_indev_t *p) {(void)p;return NULL;}
static int lv_indev_get_type(lv_indev_t *p) {(void)p;return LV_INDEV_TYPE_POINTER;}
static void lv_indev_add_event_cb(lv_indev_t *p,void (*cb)(void *),int event,void *u) {(void)p;(void)cb;(void)event;(void)u;assert(0);}
static void lv_display_add_event_cb(lv_display_t *d,void (*cb)(void *),int event,void *user) {
 (void)cb;(void)event;(void)user;assert(d);event_count++;
}
static int find_bias(struct dl_phdr_info *info,size_t size,void *user) {(void)info;(void)size;(void)user;return 0;}
static int fixture_dl(int (*cb)(struct dl_phdr_info *,size_t,void *),void *user) {(void)cb;(void)user;return 0;}
#define dl_iterate_phdr fixture_dl
static void report_cb(lv_timer_t *timer) {(void)timer;}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *),uint32_t period,void *user) {
 (void)cb;(void)user;assert(period>0);timer_count++;return &report_timer;
}
static void perfstat_ignore_timer(lv_timer_t *timer) {assert(timer==&report_timer);ignore_count++;}
static uint64_t clock_us(clockid_t clock) {(void)clock;return 1000000;}
static unsigned long long proc_ticks(void) {return 1;}
static void report_threads(char *out,size_t size,int seconds) {(void)out;(void)size;(void)seconds;thread_baselines++;}
static void usr1_handler(int signal_number) {(void)signal_number;usr1_seen=1;}
'''
source = prefix + function(production, 'sigusr2_cb')
for name, implementation in variants:
    source += implementation.replace('void perfstat_init(', 'static void ' + name + '(', 1)
source += r'''
static void child(int variant, int scenario) {
 signal(SIGUSR2,SIG_DFL);signal(SIGUSR1,usr1_handler);
 sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGUSR1);
 if(scenario==4)assert(!sigprocmask(SIG_BLOCK,&blocked,NULL));
 unsetenv("SONIX_PERF");unsetenv("SONIX_PERF_AREAS");config_seconds=0;
 if(scenario==0)setenv("SONIX_PERF","0",1);
 if(scenario==1)setenv("SONIX_PERF","-1",1);
 if(scenario==3||scenario==5)setenv("SONIX_PERF","10",1);
 if(scenario==6)config_seconds=12;
 lv_display_t screen;
 void (*initialize)(lv_display_t*)=variant==0?production_init:variant==1?missing_init:late_init;
 initialize(scenario==3?NULL:&screen);
 bool collecting=scenario==5||scenario==6;
 assert(enabled==collecting);
 assert(event_count==(collecting?5:0)&&timer_count==(collecting?1:0));
 assert(ignore_count==(collecting?1:0)&&thread_baselines==(collecting?1:0));
 assert(report_ms==(collecting?(scenario==5?10000U:12000U):0));
 assert(display==(collecting?&screen:NULL));
 struct sigaction disposition;assert(!sigaction(SIGUSR1,NULL,&disposition)&&disposition.sa_handler==usr1_handler);
 sigset_t mask;assert(!sigprocmask(SIG_SETMASK,NULL,&mask));
 assert(sigismember(&mask,SIGUSR1)==(scenario==4));
 if(scenario!=4){assert(!raise(SIGUSR1));assert(usr1_seen);}
 assert(!raise(SIGUSR2));
 assert(report_now==1&&enabled==collecting);
 report_now=0;assert(!raise(SIGUSR2));assert(report_now==1);
 assert(event_count==(collecting?5:0)&&timer_count==(collecting?1:0));
 _exit(0);
}
int main(void) {
 int positive=0,negative_terminations=0;
 for(int variant=0;variant<3;variant++)for(int scenario=0;scenario<7;scenario++) {
  pid_t pid=fork();assert(pid>=0);if(!pid)child(variant,scenario);
  int status;assert(waitpid(pid,&status,0)==pid);
  bool broken=variant==1||(variant==2&&scenario<5);
  if(broken){assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGUSR2);negative_terminations++;}
  else {assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);if(variant==0)positive++;}
 }
 printf("PASS perfstat SIGUSR2: %d production scenarios; %d missing/late-handler failures reproduced; disabled timer-free state and SIGUSR1 preserved\n",positive,negative_terminations);
 return 0;
}
'''
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='sonix-perf-signal-') as temporary:
    source_file = Path(temporary) / 'perf_signal.c'
    binary = Path(temporary) / 'perf_signal'
    source_file.write_text(source, encoding='utf-8')
    command = shlex.split(os.environ.get('CC', 'cc'))
    command += ['-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', '-g']
    command += shlex.split(os.environ.get('CFLAGS', ''))
    subprocess.run(command + [str(source_file), '-o', str(binary)], check=True)
    tested = subprocess.run([str(binary)], capture_output=True, text=True, check=True, timeout=30)
    print(tested.stdout.strip())
