"""Force pthread_create refusal in the actual file-operation admission path.

The worker is a spy: no filesystem, player or actual scheduling changes occur.
The negative control demonstrates that an inline worker fallback is rejected.
"""
from pathlib import Path
import os,re,resource,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
text=(root/'src/gui/library/filespage.c').read_text()
start=text.index('static void op_start(void) {')
end=text.index('{',start)+1;depth=1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}');end+=1
body=text[start:end]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "src/system/device/fileops.h"
enum {OP_NONE,OP_MOVE,OP_COPY,OP_DELETE,OP_RENAME,OP_MKDIR};
static int pending_op,heavy,shown,hidden,failed,timers,stops;
static int op_error;
static fileop_request_t op_request;
static storage_card_token_t action_card={7};
static unsigned action_database_epoch=12,op_database_epoch;
static char action_root[FILEOP_ROOT_MAX]="/fixture",op_source[FILEOP_PATH_MAX]="/fixture/source",op_target[FILEOP_PATH_MAX]="/fixture/target";
static bool action_current(void){return true;}
static const char *op_title(void){return "working";}
static const char *op_failure(void){return "refused";}
static bool op_running;
static uint64_t op_total_bytes,op_done_bytes;
static pthread_mutex_t op_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {int accent;} colours;
static typeof(colours) *theme(void){return &colours;}
static void gui_modal_show(void*a,int b,const char*c,void*d){(void)a;(void)b;(void)c;(void)d;shown++;}
static void gui_modal_hide(void){hidden++;}
static void gui_notify_popup(const char*s){assert(s);failed++;}
static void op_progress_stop(void){stops++;}
static void *op_worker(void *p){(void)p;heavy++;return NULL;}
static int refuse(pthread_t*t,const pthread_attr_t*a,void*(*cb)(void*),void*u){(void)t;(void)a;(void)cb;(void)u;return EAGAIN;}
#define pthread_create refuse
#define OP_PROGRESS_MS 100
static void op_progress_cb(void*p){(void)p;}
static void *op_progress_timer;
static void *lv_timer_create(void(*f)(void*),int n,void*p){(void)f;(void)n;(void)p;timers++;return NULL;}
'''
main=r'''
int main(void){
 for(int mode=OP_MOVE;mode<=OP_MKDIR;mode++){
  pending_op=mode;op_total_bytes=100;op_done_bytes=40;
  op_start();
  assert(heavy==0 && pending_op==OP_NONE && !timers);
  assert(shown==mode && hidden==mode && failed==mode && stops==mode);
  assert(op_total_bytes==0 && op_done_bytes==0);
  assert(op_request.card.epoch==7&&op_database_epoch==12&&!strcmp(op_request.root,"/fixture"));
 }
 puts("PASS file worker refusal: no inline I/O/scheduler change, modal ends, retry state cleared");
}
'''
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='sonix-files-refusal-') as folder:
    folder=Path(folder)
    for name,code in [('current',body),('inline-fallback',body.replace('const char *failure = op_failure();','op_worker(NULL);\n\t\tconst char *failure = op_failure();',1))]:
        c=folder/(name+'.c');exe=folder/name;c.write_text(prefix+code+main)
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-I'+str(root),*shlex.split(os.environ.get('CFLAGS','')),str(c),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='current':
            assert run.returncode==0,run.stderr
            print(run.stdout.strip())
        else:
            assert run.returncode<0 and 'Assertion' in run.stderr
            print('PASS negative control: inline worker fallback detected')
