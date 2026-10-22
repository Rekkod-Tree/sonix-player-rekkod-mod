"""Actual Wi-Fi queue/public-state coordinator, with inert backend/config fixtures.

From sonix-player: python3 tests/wifi_queue_test.py
No radio, subprocess, card, credentials or network calls are made by the C fixture.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]
text = (base / 'src/system/net/wifi.c').read_text()


def function(name):
    match = re.search(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t]+' + name + r'\([^;]*?\)\s*\{', text, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


start = text.index('typedef enum {\n\tJOB_NONE')
end = text.index('} job_t;', start) + len('} job_t;')
types = text[start:end]
queue_len = re.search(r'^#define JOB_QUEUE_LEN \d+', text, re.MULTILINE).group()
prefix = r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "src/system/net/wifi.h"
'''
state = r'''
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER,preference_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static bool worker_running=true;
static job_t queue[JOB_QUEUE_LEN],deferred[2];
static int queue_head,queue_count,deferred_count;
static bool power_active,scan_active,g_enabled,g_saved_enabled,g_power_busy,g_scanning;
static wifi_status_t g_status;
static int g_network_count;
static uint32_t g_serial;
static wifi_op_t g_op=WIFI_OP_IDLE;
static char g_op_ssid[WIFI_SSID_MAX];
static wifi_fail_t g_fail=WIFI_FAIL_NONE;
static wifi_opkind_t g_opkind=WIFI_OPKIND_CONNECT;
static uint64_t g_op_ticket,active_op_ticket;
static bool physical_on;
static char events[256][80];
static bool event_power[256];
static int event_count,scans,config_sets,config_saves;
static long saved_preference;
static void (*backend_hook)(job_type_t);
static void (*config_hook)(void);
static void do_power(bool);
static void do_scan(void);
static void do_connect(const char *,const char *);
static void do_disconnect(void);
static void do_forget(const char *);
static void poll_status(void) {}
static void config_set_int(const char *section,const char *key,long value) {
 assert(!strcmp(section,"wireless")&&!strcmp(key,"wifi"));saved_preference=value;config_sets++;
}
static void config_save(void) { config_saves++;if(config_hook)config_hook(); }
'''
source = prefix + types + '\n' + queue_len + '\n' + state
for name in ('set_fail', 'set_op', 'queued_type', 'refresh_activity', 'flush_deferred', 'drop_pending_scans',
             'post_job_locked', 'post_job', 'take_job', 'post_operation', 'perform_job', 'set_enabled',
             'wifi_set_enabled', 'wifi_set_enabled_transient', 'wifi_get_enabled', 'wifi_busy',
             'wifi_scan_start', 'wifi_scan_running', 'wifi_connect', 'wifi_disconnect', 'wifi_forget',
             'wifi_take_op_result', 'wifi_last_failure', 'wifi_last_op_kind'):
    source += function(name)
source += r'''
static void record(const char *event) {
 assert(event_count<256);event_power[event_count]=physical_on;snprintf(events[event_count++],sizeof(events[0]),"%s",event);
}
static void do_power(bool on) {
 record(on?"POWER ON":"POWER OFF");physical_on=on;if(backend_hook)backend_hook(JOB_POWER);
}
static void do_scan(void) {
 assert(physical_on);record("SCAN");scans++;if(backend_hook)backend_hook(JOB_SCAN);
}
static void do_connect(const char *ssid,const char *password) {
 (void)password;record(ssid);if(backend_hook)backend_hook(JOB_CONNECT);
 set_fail(physical_on?WIFI_FAIL_NONE:WIFI_FAIL_OTHER);
 set_op(physical_on?WIFI_OP_OK:WIFI_OP_FAILED,ssid);
}
static void do_disconnect(void) { record("DISCONNECT");set_op(WIFI_OP_OK,NULL); }
static void do_forget(const char *ssid) { record(ssid);set_op(WIFI_OP_OK,ssid); }
static void reset(bool on) {
 queue_head=queue_count=deferred_count=0;power_active=scan_active=false;
 g_enabled=g_saved_enabled=physical_on=on;g_power_busy=g_scanning=false;
 g_op=WIFI_OP_IDLE;g_op_ticket=active_op_ticket=0;g_op_ssid[0]=0;
 g_fail=WIFI_FAIL_NONE;g_opkind=WIFI_OPKIND_CONNECT;
 g_network_count=0;g_serial=0;memset(&g_status,0,sizeof(g_status));
 event_count=scans=config_sets=config_saves=0;saved_preference=on;backend_hook=NULL;config_hook=NULL;worker_running=true;
}
static bool step(void) {
 job_t job;pthread_mutex_lock(&lock);bool have=take_job(&job);pthread_mutex_unlock(&lock);
 if(have)perform_job(&job);
 assert(queue_count<=JOB_QUEUE_LEN&&deferred_count<=2);return have;
}
static void drain(void) { int count=0;while(step())assert(++count<64);assert(!wifi_busy()&&!wifi_scan_running()); }
static void fill(void) {
 for(int i=0;i<JOB_QUEUE_LEN;i++){char name[32];snprintf(name,sizeof(name),"AP%d",i);wifi_connect(name,"synthetic fixture");}
 assert(queue_count==JOB_QUEUE_LEN);
}
static void off_during_power(job_type_t type) {
 if(type==JOB_POWER){backend_hook=NULL;wifi_set_enabled(false);assert(wifi_busy());}
}
static void during_scan(job_type_t type) {
 if(type==JOB_SCAN){backend_hook=NULL;wifi_scan_start();wifi_set_enabled(false);assert(wifi_scan_running());}
}
static void park_during_save(void) {
 config_hook=NULL;wifi_set_enabled_transient(false);assert(!wifi_get_enabled()&&g_saved_enabled);
}
static pthread_mutex_t fixture_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_wake=PTHREAD_COND_INITIALIZER;
static bool save_entered,release_save;
static void stall_save(void) {
 pthread_mutex_lock(&fixture_lock);save_entered=true;pthread_cond_broadcast(&fixture_wake);
 while(!release_save)pthread_cond_wait(&fixture_wake,&fixture_lock);
 pthread_mutex_unlock(&fixture_lock);
}
static void *user_on(void *unused) { (void)unused;wifi_set_enabled(true);return NULL; }
static void *user_off(void *unused) { (void)unused;wifi_set_enabled(false);return NULL; }
static uint32_t random_state=0x45a125;
static uint32_t next_random(void) { random_state=random_state*1664525U+1013904223U;return random_state; }
int main(void) {
 assert(JOB_QUEUE_LEN==8);
 /* Boot uses the same checked admission, with no pre-set orphaned busy flag. */
 reset(true);job_t initial={.type=JOB_POWER,.arg_int=1};assert(post_job(&initial));assert(wifi_busy());drain();assert(scans==1);
 /* A full ordinary queue cannot swallow the latest off intent. */
 reset(true);fill();wifi_set_enabled(false);
 assert(!wifi_get_enabled()&&wifi_busy()&&deferred_count==1&&saved_preference==0);
 drain();assert(!physical_on&&event_count==9&&!strcmp(events[8],"POWER OFF"));
 /* A scan has one bounded overflow intent, no permanently orphaned spinner. */
 reset(true);fill();for(int i=0;i<20;i++)wifi_scan_start();
 assert(wifi_scan_running()&&deferred_count==1);drain();assert(scans==1&&event_count==9);
 /* Off explicitly supersedes unstarted scans, preserving all accepted joins. */
 reset(true);fill();wifi_scan_start();wifi_set_enabled(false);
 assert(!wifi_scan_running()&&deferred_count==1);drain();assert(scans==0&&!physical_on&&event_count==9);
 /* Alternating intents remain bounded, including POWER ON -> SCAN -> OFF. */
 reset(false);fill();
 for(int i=0;i<50;i++){
  wifi_set_enabled(true);wifi_scan_start();assert(deferred_count<=2);
  wifi_set_enabled(false);assert(deferred_count<=2&&!wifi_scan_running());
 }
 wifi_set_enabled(true);wifi_scan_start();drain();assert(physical_on&&saved_preference==1&&scans==1);
 /* Power coalescing must not cross a connect barrier. */
 reset(false);wifi_set_enabled(true);wifi_connect("ORDERED CONNECT",NULL);wifi_set_enabled(false);
 assert(queue_count==3);drain();
 assert(event_count==3&&!strcmp(events[0],"POWER ON")&&!strcmp(events[1],"ORDERED CONNECT")&&!strcmp(events[2],"POWER OFF"));
 /* No barrier: only the final requested power state needs hardware work. */
 reset(false);wifi_set_enabled(true);wifi_set_enabled(false);assert(queue_count==1);drain();assert(event_count==1&&!physical_on);
 /* Completing an older in-flight power request must not clear later busy. */
 reset(false);wifi_set_enabled(true);backend_hook=off_during_power;assert(step());
 assert(wifi_busy()&&!wifi_get_enabled()&&queue_count==1);drain();assert(!physical_on);
 /* Scan repeat coalesces while active; off waits for that owned scan to finish. */
 reset(true);wifi_scan_start();backend_hook=during_scan;assert(step());
 assert(scans==1&&!wifi_scan_running()&&wifi_busy());drain();assert(!physical_on);
 /* Immediate explicit scan is covered by power-on's scan, without a second sweep. */
 reset(false);wifi_set_enabled(true);wifi_scan_start();drain();assert(scans==1);
 /* An explicit scan after a connection remains after that operation barrier. */
 reset(false);wifi_set_enabled(true);wifi_connect("SCAN BARRIER",NULL);wifi_scan_start();drain();
 assert(scans==2&&event_count==4&&!strcmp(events[1],"SCAN")&&!strcmp(events[2],"SCAN BARRIER")&&!strcmp(events[3],"SCAN"));
 /* Deferred control is inserted before producers can reuse a freed queue slot. */
 reset(true);fill();wifi_set_enabled(false);assert(step());
 assert(queue_count==8&&deferred_count==0);wifi_forget("REJECTED AFTER REFILL");
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_FAILED&&wifi_last_failure()==WIFI_FAIL_REFUSED&&wifi_last_op_kind()==WIFI_OPKIND_FORGET);
 assert(step());wifi_disconnect();drain();
 assert(!strcmp(events[8],"POWER OFF")&&!strcmp(events[9],"DISCONNECT"));
 /* Old worker results must not overwrite the terminal rejection of the latest request. */
 reset(true);fill();wifi_connect("REJECTED",NULL);char ssid[WIFI_SSID_MAX];
 assert(wifi_take_op_result(ssid,sizeof(ssid))==WIFI_OP_FAILED&&!strcmp(ssid,"REJECTED"));
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_IDLE);drain();
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_IDLE&&wifi_last_failure()==WIFI_FAIL_REFUSED);
 /* Newest accepted operation reports its own kind/SSID exactly once. */
 reset(true);wifi_connect("OLDER",NULL);wifi_forget("LATEST");assert(step());
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_BUSY&&wifi_last_op_kind()==WIFI_OPKIND_FORGET);
 drain();assert(wifi_take_op_result(ssid,sizeof(ssid))==WIFI_OP_OK&&!strcmp(ssid,"LATEST"));
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_IDLE);
 /* Ring wrap and scan removal must retain every accepted command in order. */
 reset(true);fill();for(int i=0;i<3;i++)assert(step());
 for(int i=8;i<11;i++){char name[32];snprintf(name,sizeof(name),"AP%d",i);wifi_connect(name,NULL);}
 wifi_scan_start();wifi_set_enabled(false);drain();assert(event_count==12);
 for(int i=0;i<11;i++){char name[32];snprintf(name,sizeof(name),"AP%d",i);assert(!strcmp(events[i],name));}
 assert(!strcmp(events[11],"POWER OFF"));
 /* Transient parking cannot persist off, even while the user's save is running. */
 reset(false);config_hook=park_during_save;wifi_set_enabled(true);
 assert(saved_preference==1&&config_sets==1&&config_saves==1&&!wifi_get_enabled());
 wifi_set_enabled_transient(true);drain();assert(physical_on&&config_sets==1);
 wifi_set_enabled_transient(false);drain();assert(saved_preference==1&&config_sets==1);
 /* An explicit off while already parked is still remembered; stale wake cannot undo it. */
 wifi_set_enabled(false);assert(saved_preference==0&&config_sets==2);
 wifi_set_enabled_transient(true);assert(!wifi_get_enabled()&&queue_count==0);drain();assert(!physical_on);
 /* Concurrent user saves are ordered, but do not hold the Wi-Fi worker lock. */
 reset(false);save_entered=release_save=false;config_hook=stall_save;pthread_t first,second;
 assert(!pthread_create(&first,NULL,user_on,NULL));pthread_mutex_lock(&fixture_lock);
 while(!save_entered){pthread_cond_wait(&fixture_wake,&fixture_lock);}
 pthread_mutex_unlock(&fixture_lock);
 wifi_scan_start();wifi_set_enabled_transient(false);assert(!wifi_get_enabled());
 assert(!pthread_create(&second,NULL,user_off,NULL));
 pthread_mutex_lock(&fixture_lock);release_save=true;pthread_cond_broadcast(&fixture_wake);pthread_mutex_unlock(&fixture_lock);
 assert(!pthread_join(first,NULL)&&!pthread_join(second,NULL));config_hook=NULL;
 assert(!g_saved_enabled&&saved_preference==0&&config_sets==2);drain();assert(!physical_on);
 /* No worker: no orphaned power/scan state; ordinary requests get an explicit refusal. */
 reset(false);worker_running=false;wifi_set_enabled(true);wifi_scan_start();wifi_disconnect();
 assert(!wifi_get_enabled()&&!wifi_busy()&&!wifi_scan_running()&&!config_sets);
 assert(wifi_take_op_result(NULL,0)==WIFI_OP_FAILED&&wifi_last_failure()==WIFI_FAIL_REFUSED);
 /* Deterministic interleavings check FIFO and each command's power barrier. */
 for(int trial=0;trial<1000;trial++) {
  reset((trial&1)!=0);char accepted[100][32];bool required_power[100];int count=0;
  for(int action=0;action<100;action++) {
   unsigned choice=next_random()%6U;
   if(choice<2) {
    bool on=choice!=0;wifi_set_enabled(on);assert(wifi_get_enabled()==on&&saved_preference==on);
   } else if(choice==2)wifi_scan_start();
   else if(choice==3)step();
   else {
    char name[32];snprintf(name,sizeof(name),"OP-%d",action);
    if(choice==4)wifi_connect(name,NULL);else wifi_forget(name);
    if(g_op==WIFI_OP_BUSY){snprintf(accepted[count],sizeof(accepted[count]),"%s",name);required_power[count++]=g_enabled;}
   }
   assert(queue_count<=8&&deferred_count<=2);
  }
  drain();assert(physical_on==g_enabled);int seen=0;
  for(int i=0;i<event_count;i++)if(!strncmp(events[i],"OP-",3)) {
   assert(seen<count&&!strcmp(events[i],accepted[seen])&&event_power[i]==required_power[seen]);seen++;
  }
  assert(seen==count);
 }
 puts("PASS Wi-Fi queue admission, bounded control intents, FIFO barriers, latest-op ownership, transient parking and preference-save races");
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='sonix-wifi-queue-') as temporary:
    fixture = Path(temporary) / 'wifi_queue.c'
    binary = Path(temporary) / 'wifi_queue_test'
    fixture.write_text(source)
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g']
    command += shlex.split(os.environ.get('CFLAGS', ''))
    subprocess.run(command + ['-I' + str(base), str(fixture), '-pthread', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
