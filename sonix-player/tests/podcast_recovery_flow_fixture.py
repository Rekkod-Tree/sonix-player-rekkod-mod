# Generate focused tests from actual RSS recovery coordinator functions.
# GUI, worker creation and recovery backend are fixtures; not an end-to-end UI test.
# Run through tests/run_podcast_pair.sh from sonix-player.
from pathlib import Path
import re,sys
base=Path.cwd();out=Path(sys.argv[1]);s=(base/'src/gui/streaming/podcastpage.c').read_text()
def function(name):
 m=re.search(r'static [^;\n]+\b'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 i=m.end();depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[m.start():i]+'\n'
a=s.index('typedef enum {\n\tJOB_NONE');b=s.index('} job_t;',a)+len('} job_t;');types=s[a:b]
b=s.index('} recovery_note_t;')+len('} recovery_note_t;');a=s.rfind('typedef struct {',0,b);note=s[a:b]
prefix=r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/system/streaming/podcastdl.h"
typedef void lv_event_t;
static void *podcast_screen=(void *)1,*active_screen=(void *)1,*art_drop_timer;
static void *lv_screen_active(void){return active_screen;}
static void lv_timer_reset(void *p){(void)p;}
static void lv_timer_resume(void *p){(void)p;}
static bool shelf_pending;
static bool podcastcard_scan_pending(void){return shelf_pending;}
static bool worker_started,create_ok=true,download_active,storage_active,post_ok=true,fail_alloc;
static unsigned epoch=10,errors,refreshes,starts,allocated,freed;
static char last_error[96];
static podcastpair_result_t planned=PODCASTPAIR_OK;
static const char *tr(const char *key){return key;}
static void toast_error(const char *text){errors++;snprintf(last_error,sizeof(last_error),"%s",text);}
static bool show_card_now(bool recover){assert(!recover);refreshes++;return true;}
bool podcastdl_busy(void){return download_active;}
bool podcastdl_recovery_busy(void){return storage_active;}
unsigned podcaststate_card_epoch(void){return epoch;}
podcastpair_result_t podcastdl_recover_card(unsigned asked,bool (*stop)(void *),void *user){return asked!=epoch||(stop&&stop(user))?PODCASTPAIR_RECOVERY:planned;}
static void *worker_main(void *p){return p;}
static int fixture_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *p){(void)a;(void)fn;(void)p;starts++;if(create_ok)*t=(pthread_t)1;return create_ok?0:EAGAIN;}
static int fixture_detach(pthread_t t){(void)t;return 0;}
static void *fixture_calloc(size_t n,size_t size){if(fail_alloc){fail_alloc=false;return NULL;}void*p=calloc(n,size);if(p)allocated++;return p;}
static void fixture_free(void *p){if(p)freed++;free(p);}
static struct{void(*cb)(void *);void*user;}posted[16];static int posted_count;
static bool gui_post(void(*cb)(void *),void *user){if(!post_ok)return false;assert(posted_count<16);posted[posted_count].cb=cb;posted[posted_count++].user=user;return true;}
static void podcastcard_feed_loaded(unsigned ticket,int n,void *a,void*b){(void)ticket;(void)n;(void)a;(void)b;}
#define pthread_create fixture_create
#define pthread_detach fixture_detach
#define calloc fixture_calloc
#define free fixture_free
'''
states=r'''
static pthread_mutex_t job_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_ready=PTHREAD_COND_INITIALIZER;
static job_t pending;static bool have_pending;
static _Atomic unsigned recovery_ticket,recovery_done_ticket,recovery_failed_ticket,recovery_failed_epoch;
static unsigned recovery_pending_ticket;
static bool recovery_reenter,recovery_after_download,recovery_idle_retry_used,recovery_after_feed;
static job_kind_t list_kind=JOB_CARD;
static void request_card_recovery(void);
static void recovery_dropped(unsigned);
'''
text=prefix+types+'\n'+note+'\n'+states
for name in ['recovery_cancelled','recovery_view','recovery_dropped','recovery_feed_finished','submit','start_worker','request_card_recovery','recovery_done_async','recovery_allocation_failed_async','run_recovery_job','list_unloaded_cb']:text+=function(name)
# Test the exact navigation invalidation and actual finish-edge retry snippet.
f=function('run_ex');body=f[f.index('{')+1:f.index('if (job_is_list')]
text+='static void normal_navigation(void){'+body+'}\n'
f=function('dl_poll_cb');a=f.index('if (recovery_after_download');snippet=f[a:f.rfind('}')]
text+='static void finish_edge(void){download_active=false;'+snippet+'}\n'
f=function('show_card_now');a=f.index('if (recover &&');snippet=f[a:f.index('\n\treturn true;',a)]
text+='static void entry_recovery(bool recover){'+snippet+'}\n'
text+=r'''
static void worker_step(void){assert(have_pending&&pending.kind==JOB_CARD_RECOVER);job_t job=pending;have_pending=false;run_recovery_job(&job);}
static void drain(void){int n=0;while(posted_count){assert(++n<16);void(*cb)(void*)=posted[0].cb;void*u=posted[0].user;memmove(posted,posted+1,(size_t)--posted_count*sizeof(posted[0]));cb(u);}}
static void reset(void){assert(!posted_count&&allocated==freed);have_pending=false;worker_started=false;create_ok=post_ok=true;download_active=storage_active=fail_alloc=false;recovery_pending_ticket=0;recovery_reenter=recovery_after_download=recovery_idle_retry_used=recovery_after_feed=false;atomic_store(&recovery_ticket,0);atomic_store(&recovery_done_ticket,0);atomic_store(&recovery_failed_ticket,0);active_screen=podcast_screen;list_kind=JOB_CARD;errors=refreshes=starts=allocated=freed=0;last_error[0]=0;planned=PODCASTPAIR_OK;}
int main(void){
 reset();shelf_pending=false;entry_recovery(true);assert(!starts&&!have_pending&&!recovery_pending_ticket);shelf_pending=true;entry_recovery(false);assert(!starts&&!have_pending);entry_recovery(true);assert(starts==1&&have_pending);worker_step();drain();puts("PASS actual entry gate starts local recovery only after pending shelf scan");
 reset();download_active=true;request_card_recovery();assert(recovery_after_download&&!have_pending&&!starts);finish_edge();assert(have_pending);worker_step();drain();assert(refreshes==1&&!errors&&!recovery_pending_ticket&&!recovery_after_download);
 puts("PASS download finish wakes one coalesced deferred local recovery");
 reset();planned=PODCASTPAIR_BUSY;request_card_recovery();worker_step();drain();assert(have_pending&&recovery_idle_retry_used);worker_step();drain();assert(!have_pending&&recovery_after_download&&starts==1);planned=PODCASTPAIR_OK;finish_edge();worker_step();drain();assert(refreshes==1&&!errors);
 puts("PASS late BUSY catches missed finish edge once; repeated BUSY never spins");
 for(int result=PODCASTPAIR_RECOVERY;result<=PODCASTPAIR_CONFLICT;result++){reset();planned=(podcastpair_result_t)result;request_card_recovery();worker_step();drain();assert(errors==1&&refreshes==1&&!have_pending&&!recovery_after_download&&!recovery_pending_ticket);}
 puts("PASS genuine recovery failure remains visible and never requeues itself");
 reset();request_card_recovery();normal_navigation();job_t other={.kind=JOB_SEARCH};submit(&other);assert(!recovery_pending_ticket&&pending.kind==JOB_SEARCH);have_pending=false;
 reset();request_card_recovery();worker_step();request_card_recovery();assert(have_pending);drain();assert(!refreshes&&recovery_pending_ticket);worker_step();drain();assert(refreshes==1&&allocated==freed);
 puts("PASS dropped/stale queued results cannot clear a newer recovery or leak payload");
 reset();request_card_recovery();atomic_fetch_add(&recovery_ticket,1);request_card_recovery();assert(recovery_reenter);worker_step();drain();assert(have_pending&&!recovery_reenter);planned=PODCASTPAIR_OK;worker_step();drain();assert(refreshes==1);
 puts("PASS explicit re-entry during old work schedules exactly one fresh request");
 reset();request_card_recovery();worker_step();epoch++;drain();assert(!refreshes&&!errors&&!recovery_pending_ticket);
 reset();request_card_recovery();list_unloaded_cb(NULL);assert(recovery_cancelled((void*)(uintptr_t)(unsigned)pending.index));worker_step();drain();assert(!errors&&!refreshes&&!have_pending);
 puts("PASS card replacement and screen unload invalidate result and hash-cancel token");
 reset();create_ok=false;request_card_recovery();assert(!worker_started&&!have_pending&&!recovery_pending_ticket&&errors==1);create_ok=true;request_card_recovery();worker_step();drain();assert(refreshes==1);
 reset();fail_alloc=true;request_card_recovery();worker_step();drain();assert(errors==1&&!strcmp(last_error,"out_of_memory")&&!recovery_pending_ticket&&allocated==freed);
 reset();post_ok=false;request_card_recovery();worker_step();assert(allocated==freed);post_ok=true;request_card_recovery();worker_step();drain();assert(refreshes==1&&!recovery_pending_ticket);
 puts("PASS worker/payload/post failure releases pending ownership and can retry");
 reset();request_card_recovery();job_t feed={.kind=JOB_FEED};submit(&feed);assert(!recovery_pending_ticket&&recovery_after_feed&&pending.kind==JOB_FEED);have_pending=false;recovery_feed_finished();assert(have_pending&&pending.kind==JOB_CARD_RECOVER);worker_step();drain();assert(refreshes==1&&!recovery_after_feed);
reset();request_card_recovery();submit(&feed);list_unloaded_cb(NULL);have_pending=false;recovery_feed_finished();assert(!have_pending&&!recovery_after_feed);puts("PASS same-view metadata drops coalesce after feed; navigation cancels retry");
reset();atomic_store(&recovery_ticket,UINT_MAX);request_card_recovery();assert(recovery_pending_ticket==1);worker_step();drain();assert(refreshes==1);reset();
 puts("PASS ticket wrap skips zero sentinel");return 0;
}
'''
out.write_text(text)
