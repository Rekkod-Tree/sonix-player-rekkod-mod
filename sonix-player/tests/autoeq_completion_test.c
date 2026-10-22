// Actual module functions, real mutexes/GUI queues; inert file/network/LVGL seams.
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct object {int id; bool hidden;} lv_obj_t;
typedef struct event {int code; void *user;} lv_event_t;
typedef struct keyboard {int unused;} keyboard_t;
typedef struct config {const char *sd_root_path;} gui_config_t;
typedef int lv_indev_t;
enum {LV_EVENT_DELETE=1,LV_EVENT_SCREEN_UNLOAD_START=2,LV_EVENT_FOCUSED=3,LV_STATE_FOCUSED=1,LV_INDEV_STATE_PRESSED=1};
static lv_obj_t search_obj={.id=1},result_obj={.id=2},list_obj={.id=3},empty_obj={.id=4},field_obj={.id=5},home_obj={.id=6},peq_obj={.id=7};
static lv_obj_t *active=&search_obj;
static gui_config_t config={.sd_root_path="/fixture"};
static char query_text[128]="Headphones",shown_notice[512],applied_name[101]="original";
static unsigned navigations,rows_rebuilt,notices,applies,profile_loads,backend_calls,gui_checks;
static _Thread_local bool running_worker;
static bool notification_block,shutdown_on_reload,closed_fixture;
static bool thread_fail,replace_on_start_failure,index_oom,profile_oom,profile_invalid,cache_profile,replace_on_reload,backend_block;
static pthread_barrier_t entered,proceed;
static void *(*launched)(void *);static void *launched_user;static unsigned launches;
static void search_left(lv_event_t *event);
static void gui_only(void) {assert(!running_worker&&!closed_fixture);gui_checks++;}
static int lv_event_get_code(lv_event_t *event) {return event->code;}
static void *lv_event_get_user_data(lv_event_t *event) {return event->user;}
static lv_obj_t *lv_screen_active(void) {gui_only();return active;}
static void switch_screen(lv_obj_t *screen) {gui_only();if(active==&search_obj&&screen!=active){lv_event_t e={.code=LV_EVENT_SCREEN_UNLOAD_START};search_left(&e);}active=screen;navigations++;}
static void switch_screen_return_to(lv_obj_t *screen) {switch_screen(screen);}
static lv_obj_t *peqpage_screen(void) {return &peq_obj;}
static void lv_obj_clean(lv_obj_t *obj) {gui_only();assert(obj==&list_obj);rows_rebuilt++;}
static lv_obj_t *settingsrow_add(lv_obj_t *parent,const char *text,void *unused,void (*cb)(lv_event_t *),void *user) {(void)text;(void)unused;(void)user;assert(parent==&list_obj&&cb);gui_only();return &list_obj;}
static void settingsrow_name_lines(lv_obj_t *obj,int n) {(void)obj;assert(n==2);}
static void lv_obj_set_hidden(lv_obj_t *obj,bool hidden) {gui_only();obj->hidden=hidden;}
static const char *lv_textarea_get_text(lv_obj_t *obj) {assert(obj==&field_obj);return query_text;}
static void lv_textarea_set_text(lv_obj_t *obj,const char *text) {assert(obj==&field_obj);snprintf(query_text,sizeof(query_text),"%s",text);}
static void gui_page_ensure(lv_obj_t *obj) {assert(obj==&search_obj);}
static void keyboard_reset(keyboard_t *kb) {(void)kb;}
static void lv_obj_add_state(lv_obj_t *obj,int state) {(void)obj;(void)state;}
static void lv_obj_send_event(lv_obj_t *obj,int code,void *data) {(void)obj;(void)code;(void)data;}
static bool switcher_back_drag_active(void) {return false;}
static const char *tr(const char *text) {return text;}
static void thread_be_background(const char *name) {(void)name;}
static void sx_toast_busy(const char *,bool);
static int fake_create(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void *),void *user) {(void)thread;(void)attr;launches++;if(thread_fail){if(replace_on_start_failure)sx_toast_busy("Newer operation",false);return EAGAIN;}*thread=(pthread_t)1;launched=fn;launched_user=user;return 0;}
static int fake_detach(pthread_t thread) {(void)thread;return 0;}

// Count owned job/nested payload lifetimes. No allocation merely to publish.
static struct {void *pointer;bool live;} tracked[256];static unsigned allocated,freed;static bool fail_calloc;
static void *track(void *pointer) {assert(pointer&&allocated<256);tracked[allocated++]=(typeof(tracked[0])){pointer,true};return pointer;}
static void *fixture_calloc(size_t n,size_t size) {if(fail_calloc){fail_calloc=false;return NULL;}return track(calloc(n,size));}
static char *text_alloc(const char *text) {return track(strdup(text));}
static void fixture_free(void *pointer) {if(!pointer)return;for(unsigned i=0;i<allocated;i++)if(tracked[i].pointer==pointer&&tracked[i].live){tracked[i].live=false;freed++;free(pointer);return;}assert(!"Double free or unowned payload");}

// Ownership functions are production; the separate real-LVGL test covers rendering.
static uint32_t generation;static uint64_t owner_generation;
static bool press_tracking,undo_ready,held,busy,toast_visible;
static lv_indev_t *press_indev;static lv_obj_t *press_target;
static int lv_indev_get_state(lv_indev_t *input) {(void)input;return 0;}
static void lv_indev_wait_release(lv_indev_t *input) {(void)input;}
static void sx_toast_busy(const char *,bool);
static void sx_toast_dismiss(void);
static bool sx_toast_busy_showing(void);
#include "actual_toast_owner.inc"
static void sx_toast_busy(const char *text,bool dismissable) {(void)dismissable;gui_only();invalidate_input();busy=toast_visible=true;snprintf(shown_notice,sizeof(shown_notice),"%s",text);}
static void sx_toast_dismiss(void) {gui_only();invalidate_input();busy=toast_visible=false;}
static bool sx_toast_busy_showing(void) {gui_only();return busy&&toast_visible;}
static void toast_success(const char *text) {gui_only();invalidate_input();busy=false;toast_visible=true;snprintf(shown_notice,sizeof(shown_notice),"%s",text);notices++;}
static void toast_error(const char *text) {toast_success(text);}
static bool toast_busy_showing(void) {return sx_toast_busy_showing();}

#define GUI_POST_MAX 32
#define GUI_LATEST_MAX 4
typedef struct {void (*cb)(void *);void *user;} gui_post_t;
static gui_post_t post_ring[GUI_POST_MAX],post_latest[GUI_LATEST_MAX];
static int post_count,post_head,post_wake_fd=-1;static pthread_mutex_t post_lock=PTHREAD_MUTEX_INITIALIZER;
#include "actual_gui_queues.inc"
static bool gui_post(void (*cb)(void *),void *user) {if(notification_block){pthread_barrier_wait(&entered);pthread_barrier_wait(&proceed);}return actual_gui_post(cb,user);}
typedef struct {uint64_t epoch;} storage_card_token_t;
typedef struct {int ignored;} peq_preset_data_t;
#include "actual_job_types.inc"
#include "actual_gui_storage.inc"

// I/O is covered by the separate actual-module filesystem fixture.
static bool load_search_results(job_t *job) {
 backend_calls++;if(backend_block){pthread_barrier_wait(&entered);pthread_barrier_wait(&proceed);}
 if(index_oom){strcpy(job->error,"out_of_memory");return false;}
 char *bytes=text_alloc("Index");fixture_free(bytes);job->result_count=1;
 strcpy(job->results[0].display,"Headphones");return true;
}
static bool update_index(job_t *job) {(void)job;return true;}
static bool download_profile(job_t *job) {
 backend_calls++;if(backend_block){pthread_barrier_wait(&entered);pthread_barrier_wait(&proceed);}
 if(profile_oom||profile_invalid){strcpy(job->error,"profile error");return false;}
 job->profile_text=text_alloc("Filters");job->profile_size=7;job->published=true;return true;
}
static void preset_name(const profile_t *selected,char *out,size_t size) {(void)selected;snprintf(out,size,"Fixture preset");}
static void cache_path(const profile_t *selected,char *out,size_t size) {(void)selected;snprintf(out,size,"12345678.txt");}
static bool peq_preset_apply_parsed(const char *name,const peq_preset_data_t *data,bool enabled) {gui_only();assert(name&&data&&enabled);profile_loads++;snprintf(applied_name,sizeof(applied_name),"%s",name);applies++;return true;}
static bool peqautoeq_network_needed(void) {return false;}
static void result_clicked(lv_event_t *event);
void peqautoeq_service_completion(void);
static void sx_toast_busy(const char *,bool);
static void sx_toast_dismiss(void);
static bool sx_toast_busy_showing(void);
#define pthread_create fake_create
#define pthread_detach fake_detach
#define calloc fixture_calloc
#define free fixture_free
#define stat(...) fixture_stat(__VA_ARGS__)
#define rename fixture_rename
#define remove fixture_remove
#include "actual_autoeq_functions.inc"
#undef pthread_create
#undef pthread_detach
#undef calloc
#undef free
#undef stat
#undef rename
#undef remove

static void noop(void *user) {(void)user;}
static void latest1(void *u){(void)u;}static void latest2(void *u){(void)u;}static void latest3(void *u){(void)u;}static void latest4(void *u){(void)u;}
static void fill_queues(void) {for(int i=0;i<GUI_POST_MAX;i++)assert(gui_post(noop,NULL));assert(gui_post_latest(latest1,NULL));assert(gui_post_latest(latest2,NULL));assert(gui_post_latest(latest3,NULL));assert(gui_post_latest(latest4,NULL));}
static void drain_fifo(void) {while(post_count){gui_post_t item=post_ring[post_head];post_head=(post_head+1)%GUI_POST_MAX;post_count--;item.cb(item.user);}}
typedef struct {void *(*fn)(void *);void *user;} worker_call_t;
static void *real_worker_call(void *arg) {worker_call_t *call=arg;running_worker=true;void *result=call->fn(call->user);running_worker=false;return result;}
static void run_worker(void) {assert(launched);void *(*fn)(void *)=launched;void *arg=launched_user;launched=NULL;running_worker=true;fn(arg);running_worker=false;}
static job_t *new_job(job_action_t action) {job_t *job=fixture_calloc(1,sizeof(*job));assert(job);job->action=action;strcpy(job->preset,"Fixture preset");strcpy(job->cache,"/fixture/Profile.txt");strcpy(job->query,"Headphones");return job;}
static void reload(void) {if(replace_on_reload)sx_toast_busy("Newer operation",false);if(shutdown_on_reload){peqautoeq_shutdown_completions();closed_fixture=true;}}
static void leave_search(void) {lv_event_t event={.code=LV_EVENT_SCREEN_UNLOAD_START};search_left(&event);active=&home_obj;}
static void assert_clean(void) {assert(allocated==freed);for(unsigned i=0;i<allocated;i++)assert(!tracked[i].live);}
static void reset(void) {
 assert(!ready_job&&!job_busy);assert_clean();allocated=freed=0;memset(tracked,0,sizeof(tracked));
 job_closing=false;closed_fixture=shutdown_on_reload=false;reset_storage();search_generation=1;pending_results_generation=pending_notice_generation=0;pending_notice[0]=0;
 search_screen=&search_obj;results_screen=&result_obj;search_field=&field_obj;search_list=&list_obj;results_empty=&empty_obj;cfg=&config;
 active=&search_obj;query_text[0]=0;strcpy(query_text,"Headphones");navigations=rows_rebuilt=notices=applies=profile_loads=backend_calls=launches=gui_checks=0;
 index_oom=profile_oom=profile_invalid=thread_fail=replace_on_start_failure=replace_on_reload=backend_block=fail_calloc=notification_block=false;cache_profile=false;busy=toast_visible=false;launched=NULL;post_count=post_head=0;memset(post_latest,0,sizeof(post_latest));strcpy(applied_name,"original");reload_cb=reload;
}
int main(void) {
 reset();
 for(int i=0;i<100;i++){peqautoeq_service_completion();}assert(!allocated&&!gui_checks);
 // Normal accepted notification owns NULL; a duplicate has no second payload.
 assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();assert(job_busy&&ready_job&&post_count==1&&post_ring[post_head].user==NULL);
 drain_fifo();assert(!job_busy&&!ready_job&&rows_rebuilt==1&&active==&result_obj);unsigned n=freed;job_done(NULL);assert(freed==n);assert_clean();
 reset();fill_queues();unsigned count=allocated;assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();
 assert(job_busy&&ready_job&&allocated==count+2); // Job plus index bytes; no completion wrapper.
 job_t *rejected=new_job(JOB_UPDATE_DATABASE);rejected->profile_text=text_alloc("Owned refused data");assert(!start_job(rejected,"Other"));assert(job_busy&&ready_job);
 peqautoeq_service_completion();assert(active==&result_obj&&!job_busy&&rows_rebuilt==1);drain_fifo();job_done(NULL);assert_clean();
 reset();assert(start_job(new_job(JOB_SEARCH),"Searching"));leave_search();run_worker();peqautoeq_service_completion();assert(!rows_rebuilt&&!navigations&&active==&home_obj);assert_clean();
 reset();assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();leave_search();peqautoeq_open();navigations=0;peqautoeq_service_completion();assert(!rows_rebuilt&&!navigations);assert_clean();
 reset();assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();uint64_t newer=0;sx_toast_busy("Newer operation",false);newer=owner_generation;
 peqautoeq_service_completion();assert(!navigations&&rows_rebuilt==1&&pending_results_generation&&busy&&owner_generation==newer&&!strcmp(shown_notice,"Newer operation"));
 for(int i=0;i<20;i++){peqautoeq_service_completion();}assert(!navigations&&busy);
 sx_toast_busy_end();peqautoeq_service_completion();assert(active==&result_obj&&navigations==1);assert_clean();
 reset();assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();sx_toast_busy("Newer operation",false);peqautoeq_service_completion();leave_search();sx_toast_busy_end();peqautoeq_service_completion();assert(!navigations&&active==&home_obj);assert_clean();
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();leave_search();sx_toast_busy("Newer operation",false);newer=owner_generation;
 peqautoeq_service_completion();assert(applies==1&&!strcmp(applied_name,"Fixture preset")&&!navigations&&busy&&owner_generation==newer&&pending_notice[0]);assert_clean();
 sx_toast_busy_end();peqautoeq_service_completion();assert(!notices&&active==&home_obj);peqautoeq_open();peqautoeq_service_completion();assert(notices==1&&strstr(shown_notice,"profile_applied"));
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();leave_search();peqautoeq_service_completion();assert(applies==1&&!navigations&&active==&home_obj&&notices==1);assert_clean();
 reset();replace_on_reload=true;assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();peqautoeq_service_completion();assert(applies==1&&busy&&!navigations&&!strcmp(shown_notice,"Newer operation"));assert_clean();
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();peqautoeq_service_completion();assert(applies==1&&active==&peq_obj&&notices==1);assert_clean();
 reset();index_oom=true;assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();peqautoeq_service_completion();assert(notices==1&&!applies&&!job_busy&&!strcmp(applied_name,"original"));assert_clean();
 reset();profile_oom=true;assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();peqautoeq_service_completion();assert(notices==1&&!applies&&!profile_loads&&!strcmp(applied_name,"original"));assert_clean();
 reset();profile_invalid=true;assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();peqautoeq_service_completion();assert(notices==1&&!applies&&!strcmp(applied_name,"original"));assert_clean();
 reset();thread_fail=true;assert(!start_job(new_job(JOB_SEARCH),"Searching"));assert(!job_busy&&!ready_job&&!busy&&notices==1);assert_clean();
 reset();thread_fail=replace_on_start_failure=true;assert(!start_job(new_job(JOB_SEARCH),"Searching"));assert(!job_busy&&busy&&!strcmp(shown_notice,"Newer operation")&&pending_notice[0]);assert_clean();
 reset();fail_calloc=true;search_accept(NULL);assert(!job_busy&&!launches&&notices==1);assert_clean();
 reset();result_count=1;fail_calloc=true;lv_event_t clicked={.user=0};result_clicked(&clicked);assert(!job_busy&&!launches&&notices==1);assert_clean();
 reset();fail_calloc=true;update_database(NULL);assert(!job_busy&&!launches&&notices==1);assert_clean();
 reset();index_oom=true;assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();sx_toast_busy("Newer operation",false);peqautoeq_service_completion();assert(pending_notice[0]);
 assert(start_job(new_job(JOB_UPDATE_DATABASE),"New request"));assert(!pending_notice[0]);run_worker();peqautoeq_service_completion();assert(!pending_notice[0]);assert_clean();
 // GUI may consume/free a published job before its worker posts the wake.
 reset();notification_block=true;pthread_barrier_init(&entered,NULL,2);pthread_barrier_init(&proceed,NULL,2);
 assert(start_job(new_job(JOB_SEARCH),"Searching"));pthread_t early_thread;worker_call_t early={launched,launched_user};assert(!pthread_create(&early_thread,NULL,real_worker_call,&early));
 pthread_barrier_wait(&entered);assert(ready_job&&job_busy);peqautoeq_service_completion();assert(!job_busy);assert_clean();
 notification_block=false;assert(start_job(new_job(JOB_UPDATE_DATABASE),"New request"));run_worker();assert(ready_job&&job_busy);
 pthread_barrier_wait(&proceed);assert(!pthread_join(early_thread,NULL));drain_fifo();assert(!ready_job&&!job_busy&&notices==1);assert_clean();
 pthread_barrier_destroy(&entered);pthread_barrier_destroy(&proceed);
 reset();assert(start_job(new_job(JOB_SEARCH),"Searching"));run_worker();unsigned before_notices=notices;peqautoeq_shutdown_completions();job_done(NULL);assert(notices==before_notices&&!rows_rebuilt&&!job_busy);assert_clean();
 reset();shutdown_on_reload=true;assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));run_worker();peqautoeq_service_completion();assert(job_closing&&applies==1&&!notices&&!navigations);assert_clean();
 // Publish after GUI shutdown: the worker reclaims private data and posts nothing.
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));peqautoeq_shutdown_completions();run_worker();assert(!job_busy&&!ready_job&&!post_count&&!applies);assert_clean();
 // Closing interleaving uses real pthread barriers, no scheduling sleeps.
 reset();backend_block=true;pthread_barrier_init(&entered,NULL,2);pthread_barrier_init(&proceed,NULL,2);
 assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Downloading"));pthread_t thread;worker_call_t call={launched,launched_user};assert(!pthread_create(&thread,NULL,real_worker_call,&call));pthread_barrier_wait(&entered);
 peqautoeq_shutdown_completions();pthread_barrier_wait(&proceed);assert(!pthread_join(thread,NULL));assert(!job_busy&&!ready_job&&!post_count&&!applies);assert_clean();
 pthread_barrier_destroy(&entered);pthread_barrier_destroy(&proceed);assert(!start_job(new_job(JOB_SEARCH),"Closed"));assert_clean();

 reset();pthread_mutex_lock(&card_lock);assert(start_job(new_job(JOB_SEARCH),"Wait admission"));job_t *waiting=ready_job;
 assert(waiting&&waiting->phase==JOB_NEEDS_ADMISSION&&!launches&&job_busy&&busy);
 for(int i=0;i<1000;i++)peqautoeq_service_completion();
 assert(ready_job==waiting&&!launches&&job_busy&&busy);pthread_mutex_unlock(&card_lock);
 peqautoeq_service_completion();assert(launches==1&&!ready_job&&job_busy);run_worker();peqautoeq_service_completion();assert_clean();
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Wait READY"));run_worker();waiting=ready_job;unsigned free_before=freed;
 pthread_mutex_lock(&card_lock);for(int i=0;i<1000;i++)peqautoeq_service_completion();
 assert(ready_job==waiting&&freed==free_before&&job_busy&&busy&&!applies);pthread_mutex_unlock(&card_lock);
 peqautoeq_service_completion();assert(applies==1);assert_clean();
 reset();assert(start_job(new_job(JOB_DOWNLOAD_PROFILE),"Old A"));run_worker();change_owner();peqautoeq_service_completion();
 assert(!applies&&!strcmp(applied_name,"original")&&notices==1&&!job_busy);assert_clean();
 reset();pthread_mutex_lock(&card_lock);assert(start_job(new_job(JOB_SEARCH),"Pending leave"));leave_search();
 peqautoeq_service_completion();assert(!job_busy&&!ready_job&&!launches);pthread_mutex_unlock(&card_lock);assert_clean();
 reset();pthread_mutex_lock(&card_lock);assert(start_job(new_job(JOB_SEARCH),"Pending close"));peqautoeq_shutdown_completions();
 assert(!job_busy&&!ready_job&&!launches);pthread_mutex_unlock(&card_lock);assert_clean();
 puts("PASS actual AutoEQ ownership: both queues full, direct service, stale search, owned toast/global apply, OOM/start failure, closing, reentry, duplicate wake and barrier publication");
 return 0;
}
