"""Production library/file completion paths with refused GUI wake and shutdown.

python3 tests/library_completion_test.py
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' enables sanitizers.
All storage and notification operations are mocks; no card/device is accessed.
"""
from pathlib import Path
import os,re,resource,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
playlist=(root/'src/gui/library/playlistpage.c').read_text()
files=(root/'src/gui/library/filespage.c').read_text()
def function(source,name):
    m=re.search(r'^(?:static )?[^\n;]*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',source,re.M)
    assert m,name
    end,depth=m.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]+'\n'
headers=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
static int posts,successes,errors,frees;
static unsigned long long owner,live_owner;
static void release(void*p){if(p)frees++;free(p);}
#define free release
static bool gui_post(void(*cb)(void*),void*p){(void)cb;(void)p;posts++;return false;}
static void gui_notify_popup(const char*s){assert(s);errors++;}
static void toast_success(const char*s){assert(s);successes++;}
static void toast_error(const char*s){assert(s);errors++;}
'''
prefix=headers+r'''
#define PLAYLIST_NAME_MAX 127
static char **pending_many;
static int pending_many_count;
static void pending_many_free(void){for(int i=0;i<pending_many_count;i++)free(pending_many[i]);free(pending_many);pending_many=NULL;pending_many_count=0;}
static uint64_t toast_busy_owned(const char*s){assert(s);live_owner=++owner;return live_owner;}
static bool toast_busy_end_owned(uint64_t id){if(id!=live_owner)return false;live_owner=0;return true;}
static void thread_be_low_priority(const char*s){(void)s;}
static bool playlists_exists(const char*s){(void)s;return true;}
static bool playlists_create(const char*s){(void)s;return true;}
static bool playlists_add_track(const char*s,const char*p){(void)s;(void)p;return true;}
typedef struct{const char *path,*name,*artist;}library_fav_row_t;
static void playlists_track_names(const char*p,char*t,size_t nt,char*a,size_t na){snprintf(t,nt,"%s",p);snprintf(a,na,"artist");}
static int library_fav_add_many(const library_fav_row_t*r,int n){assert(r);return n;}
static int refuse(pthread_t*t,const pthread_attr_t*a,void*(*cb)(void*),void*p){(void)t;(void)a;(void)cb;(void)p;return EAGAIN;}
#define pthread_create refuse
void playlistpage_service_completion(void);
'''
block=playlist[playlist.index('#define BATCH_RESULTS_MAX'):playlist.index('// The naming dialog.')]
main=r'''
static int applied,disposed;
static void apply(void*p){assert(p);applied++;}
static void dispose(void*p){assert(p);disposed++;}
int main(void){
 int values[BATCH_RESULTS_MAX+1];
 for(int i=0;i<BATCH_RESULTS_MAX;i++)assert(batch_result_admit(&values[i],apply,dispose)==i);
 assert(batch_result_admit(&values[BATCH_RESULTS_MAX],apply,dispose)==-1);
 for(int i=0;i<BATCH_RESULTS_MAX;i++)batch_result_publish(i);
 assert(posts==BATCH_RESULTS_MAX&&applied==0);
 for(int i=0;i<BATCH_RESULTS_MAX;i++)playlistpage_service_completion();
 assert(applied==BATCH_RESULTS_MAX&&!atomic_load(&batch_results_ready));
 playlistpage_service_completion();assert(applied==BATCH_RESULTS_MAX);
 batch_t*b=calloc(1,sizeof(*b));b->count=2;b->paths=calloc(2,sizeof(char*));b->paths[0]=strdup("one");b->paths[1]=strdup("two");
 b->toast_owner=toast_busy_owned("add");b->result_slot=batch_result_admit(b,batch_done_cb,batch_free);assert(b->result_slot>=0);
 int before=frees;batch_worker(b);assert(b->added==2&&!b->paths&&frees-before==3);
 playlistpage_service_completion();assert(frees-before==4&&successes==1);
 fav_batch_t*f=calloc(1,sizeof(*f));f->count=1;f->paths=calloc(1,sizeof(char*));f->paths[0]=strdup("one");
 f->toast_owner=toast_busy_owned("star");f->result_slot=batch_result_admit(f,fav_done_cb,fav_free);assert(f->result_slot>=0);
 before=frees;fav_worker(f);assert(f->starred==1&&!f->paths&&frees-before==5);
 playlistpage_service_completion();assert(frees-before==6&&successes==2);
 const char *paths[]={"one"};
 assert(!playlistpage_add_tracks_to("list",paths,1));assert(errors==1);
 pending_many=calloc(1,sizeof(char*));pending_many[0]=strdup("one");pending_many_count=1;
 assert(!batch_start("list"));assert(errors==2&&!pending_many&&!pending_many_count);
 playlistpage_add_favourites(paths,1);assert(errors==3);
 for(int i=0;i<BATCH_RESULTS_MAX;i++)assert(!batch_results[i].payload);
 int ready=batch_result_admit(&values[0],apply,dispose),running=batch_result_admit(&values[1],apply,dispose);
 batch_result_publish(ready);playlistpage_shutdown_completions();assert(disposed==1);
 batch_result_publish(running);assert(disposed==2);playlistpage_service_completion();assert(applied==BATCH_RESULTS_MAX);
 assert(batch_result_admit(&values[2],apply,dispose)==-1);
 for(int i=0;i<BATCH_RESULTS_MAX;i++)assert(!batch_results[i].payload);
 puts("PASS playlist/favourites: full admission, refused wake, exactly-once delivery, off-UI path disposal, worker refusal, ready/running shutdown");
}
'''
file_prefix=headers+r'''
#include "src/system/device/fileops.h"
enum{OP_NONE,OP_MOVE,OP_COPY,OP_DELETE,OP_RENAME,OP_MKDIR};
static int pending_op,hidden,rebuilt,progress_stops;
static int op_error;
static fileop_request_t op_request;
static unsigned op_database_epoch=12;
static bool op_ok,op_running;
static atomic_bool op_ready,op_closing;
static pthread_mutex_t op_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t op_total_bytes,op_done_bytes;
static void op_progress_stop(void){progress_stops++;}
static void gui_modal_hide(void){hidden++;}
void storage_card_files_recheck(void){}
static void rebuild(void){rebuilt++;}
static void thread_be_background(const char*s){(void)s;}
static int prune_calls,pin_calls,pinned_fd=-1;
static bool stale_card;
int storage_pin_card_if(const storage_card_token_t*t){assert(t->epoch==31);pin_calls++;if(stale_card){errno=ESTALE;return -1;}pinned_fd=open("/dev/null",O_RDONLY);assert(pinned_fd>=0);return pinned_fd;}
static int library_forget_deleted_at(unsigned epoch,const char *const*p,int n){assert(epoch==12&&p&&n==1&&fcntl(pinned_fd,F_GETFD)>=0);prune_calls++;return 1;}
int fileop_run(const fileop_request_t*r,fileop_progress_fn progress,fileop_cancel_fn cancelled,void*u){assert(r);if(cancelled(u))return ECANCELED;progress(100,100,u);return 0;}
'''
file_code=''.join(function(files,n) for n in ['op_failure','op_progress','op_cancelled','op_finished','filespage_service_completion','filespage_shutdown_completions','op_completion_wakeup','op_worker'])
file_main=r'''
int main(void){
 (void)frees;(void)owner;(void)live_owner;(void)op_done_bytes;(void)toast_error;(void)release;
 pending_op=OP_COPY;op_running=true;op_worker(NULL);
 assert(posts==1&&op_ok&&pending_op==OP_COPY&&hidden==0);
 filespage_service_completion();assert(!op_running&&pending_op==OP_NONE&&hidden==1&&rebuilt==1&&successes==1);
 filespage_service_completion();assert(hidden==1&&rebuilt==1);
 op_request.kind=FILEOP_DELETE;op_request.card.epoch=31;pending_op=OP_DELETE;op_running=true;op_worker(NULL);
 assert(prune_calls==1&&pin_calls==1&&fcntl(pinned_fd,F_GETFD)==-1&&errno==EBADF);
 filespage_service_completion();assert(hidden==2&&rebuilt==2&&posts==2);
 stale_card=true;pending_op=OP_DELETE;op_running=true;op_worker(NULL);
 assert(prune_calls==1&&pin_calls==2);filespage_service_completion();
 pending_op=OP_COPY;op_request.kind=FILEOP_COPY;op_running=true;filespage_shutdown_completions();op_worker(NULL);filespage_service_completion();
 assert(hidden==3&&rebuilt==3&&posts==3);
 puts("PASS file completion: refused wake retains result/modal ownership, duplicate and late callbacks harmless");
}
'''
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='sonix-library-completion-') as folder:
    folder=Path(folder)
    cases=[('playlist',prefix+block+main,False),('files',file_prefix+file_code+file_main,False),
           ('lost-playlist-result',prefix+block.replace('batch_results[slot].ready = true;','batch_results[slot].ready = false;',1)+main,True),
           ('lost-file-result',file_prefix+file_code.replace('atomic_store(&op_ready, true);','atomic_store(&op_ready, false);',1)+file_main,True)]
    for name,code,negative in cases:
        c=folder/(name+'.c');exe=folder/name;c.write_text(code)
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-I'+str(root),*shlex.split(os.environ.get('CFLAGS','')),str(c),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        if negative:
            assert run.returncode<0 and 'Assertion' in run.stderr,(name,run.stderr)
            print('PASS negative control:',name)
        else:
            assert run.returncode==0,(name,run.stderr)
            print(run.stdout.strip())
