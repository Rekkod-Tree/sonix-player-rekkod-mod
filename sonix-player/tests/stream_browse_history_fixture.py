# Generate focused history/key tests from the actual service-page functions.
# The page loading/transport shell is synthetic; this is not end-to-end API UI.
# Run via tests/run_svclist_anchor.sh from sonix-player.
from pathlib import Path
import sys
base=Path.cwd();out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def function(text,name):
 start=text.index('static ',text.index('static ',0))
 import re
 m=re.search(r'static [^;\n]+\b'+name+r'\([^;]*?\)\s*\{',text)
 assert m,name
 a=m.start();i=m.end();depth=1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[a:i]+'\n'
for svc in ['yandex','zvuk']:
 s=(base/f'src/gui/streaming/{svc}page.c').read_text();u=svc.upper()
 start=s.index('typedef struct {',s.index('// going back inside the list page') if svc=='yandex' else s.index('// --- history inside'))
 stop=s.index('static void show_list(const list_step_t *step);',start)
 fragment=s[start:stop]
 row_start=s.index('typedef enum { ROW_TRACK');row_end=s.index('} row_t;',row_start)+len('} row_t;')
 row_types=s[row_start:row_end]
 prefix='''#define main svclist_fixture_main
#include "tests/svclist_anchor_test.c"
#undef main
'''+f'#include "src/system/streaming/{svc}.h"\n#include "src/system/streaming/{svc}list.h"\n'+row_types+'\n'+'''typedef enum { JOB_NONE, JOB_WAVE, JOB_SEARCH, JOB_ALBUM, JOB_ARTIST, JOB_BROWSE_BOOKS } job_kind_t;
static svclist_t *list;
static lv_obj_t *list_screen;
static unsigned list_generation;
static struct { unsigned generation; SERVICE_LIST *list; int count; row_t rows[5]; } delivered, *shown = &delivered;
'''
 prefix=prefix.replace('SERVICE_LIST',svc+'list_t')
 local_ids=f'int {svc}list_count({svc}list_t *l) {{ (void)l;return shown->count; }}\n'
 if svc=='yandex':
  local_ids+='bool yandexlist_ids(yandexlist_t *l,int index,char *out,size_t size,char *album,size_t album_size) { (void)l;(void)album;(void)album_size;snprintf(out,size,"%d",ids[index]);return true; }\n'
 else:
  local_ids+='bool zvuklist_id(zvuklist_t *l,int index,char *out,size_t size) { (void)l;snprintf(out,size,"%d",ids[index]);return true; }\n'
 body=prefix+local_ids+function(s,'view_count')+function(s,'list_key')+fragment+'''static void show_list(const list_step_t *step) {
 svclist_set_count(list,0,false);
 list_generation++;
 list_restore_pending=step->anchor.valid;
 list_restore_generation=list_generation;
}
'''
 if svc == 'zvuk':
  body += 'bool switcher_back_drag_active(void) { return false; }\n'
 names=['fill_list_count','same_list','list_back_guard','open_list'] + (['search_pill_cb','books_pill_cb'] if svc=='zvuk' else [])
 for name in names:
  body+=function(s,name)
 # Exercise the actual JOB_PAGE branch, with its external UI sinks observed.
 callback=function(s,'job_done_async')
 marker='if (r->kind == JOB_PAGE)' if svc=='yandex' else 'case JOB_PAGE:'
 a=callback.index('{',callback.index(marker));i=a+1;depth=1
 while depth:
  if callback[i]=='{':depth+=1
  elif callback[i]=='}':depth-=1
  i+=1
 branch=callback[a+1:i-1]
 body+='#include <time.h>\n typedef struct { SERVICE_LIST *list; unsigned generation; int page; char error[2]; } page_result_t;\n static int page_asked, page_failed, freed_pages, explicit_visible, bars_updated;\n static time_t page_failed_at;\n static void free_result(page_result_t *r) { (void)r;freed_pages++; }\n static void list_visible(int first,int last) { (void)first;(void)last;explicit_visible++; }\n static void update_album_bar(void) { bars_updated++; }\n static void page_callback(page_result_t *r) {\n'+branch+'}\n'
 call=lambda title,kind,a,b,n:f'open_list("{title}", {kind}, {a}, {b}'+(f', {n}' if svc=='zvuk' else '')+');'
 body+='''static void arrive(int count) {
 delivered.generation=list_generation;delivered.count=count;
 fill_list_count(count);
}
int main(int argc,char **argv) {
 svclist_fixture_main(argc,argv);
 for(int i=0;i<20000;i++)ids[i]=i+100;
 grouped=false;large=false;
 // Actual service key callback: compact IDs and already delivered mixed rows.
 delivered.list=(SERVICE_LIST *)1;delivered.count=20000;
 char key_a[256],key_b[256];assert(list_key(12340,key_a,sizeof(key_a)));
 assert(!list_key(12340,key_b,2));
 delivered.list=NULL;delivered.count=2;
 memset(delivered.rows,0,sizeof(delivered.rows));
 delivered.rows[0].kind=ROW_TRACK;snprintf(delivered.rows[0].u.track.id,sizeof(delivered.rows[0].u.track.id),"42");
 delivered.rows[1].kind=ROW_ALBUM;snprintf(delivered.rows[1].u.album.id,sizeof(delivered.rows[1].u.album.id),"42");
 assert(list_key(0,key_a,sizeof(key_a))&&list_key(1,key_b,sizeof(key_b))&&strcmp(key_a,key_b));
 delivered.count=0;

 svclist_cbs_t cbs={.bind=bind,.key=key,.visible=visible};
 list_screen=lv_screen_active();
 list=svclist_create(list_screen,NULL,&cbs,24,false);assert(list);
 lv_obj_set_size(svclist_obj(list),440,550);
'''+call('Search','JOB_SEARCH','"query"','NULL',2)+'''
 arrive(20000);move_to(list,12340,21);
'''+call('Album','JOB_ALBUM','"album42"','NULL',0)+'''
 assert(list_history_len==1);arrive(30);
 assert(list_back_guard());assert(list_current.kind==JOB_SEARCH);
 assert(svclist_count(list)==0);arrive(20000);
 svclist_anchor_t a;assert(svclist_capture(list,&a));assert(a.index==12340&&a.offset==21);
 // Mutable bookmarks do not create false navigation identities.
 list_step_t same=list_current;memset(&same.anchor,0,sizeof(same.anchor));assert(same_list(&same,&list_current));
'''+call('Artist','JOB_ARTIST','"artist7"','NULL',0)+'''
 arrive(80);move_to(list,40,9);
'''+call('Album','JOB_ALBUM','"album43"','NULL',0)+'''
 arrive(30);assert(list_back_guard());arrive(80);
 assert(svclist_capture(list,&a)&&a.index==40&&a.offset==9);
 assert(list_back_guard());arrive(20000);
 assert(svclist_capture(list,&a)&&a.index==12340&&a.offset==21);
 // An unrelated generation may not consume/apply a pending bookmark.
 list_restore_pending=true;list_restore_generation=list_generation-1;
 svclist_set_count(list,20000,false);fill_list_count(20000);
 assert(lv_obj_get_scroll_y(svclist_obj(list))==0&&list_restore_pending);
 list_restore_generation=list_generation;fill_list_count(20000);
 assert(!list_restore_pending&&svclist_capture(list,&a)&&a.index==12340);
'''+call('New search','JOB_SEARCH','"different query"','NULL',1)+'''
 arrive(20000);assert(lv_obj_get_scroll_y(svclist_obj(list))==0);
'''
 if svc=='zvuk':
  body+='''list_step_t child=list_current;child.n++;assert(!same_list(&child,&list_current));
 snprintf(child.b,sizeof(child.b),"children");assert(!same_list(&child,&list_current));
// Actual filter callbacks must discard bookmarks and previous book-page paths.
lv_obj_t *pill=lv_obj_create(list_screen);
lv_obj_add_event_cb(pill,search_pill_cb,LV_EVENT_CLICKED,(void *)(intptr_t)2);
move_to(list,300,7);svclist_capture(list,&list_current.anchor);
lv_obj_send_event(pill,LV_EVENT_CLICKED,NULL);arrive(20000);
assert(!list_current.anchor.valid&&lv_obj_get_scroll_y(svclist_obj(list))==0);
lv_obj_delete(pill);
open_list("Books",JOB_BROWSE_BOOKS,NULL,NULL,0);arrive(80);move_to(list,40,11);
open_list("More books",JOB_BROWSE_BOOKS,NULL,NULL,60);arrive(80);move_to(list,41,12);
open_list("Book",JOB_ALBUM,"book",NULL,0);arrive(20);
assert(list_back_guard());arrive(80);
assert(svclist_capture(list,&a)&&a.index==41&&a.offset==12);
pill=lv_obj_create(list_screen);
lv_obj_add_event_cb(pill,books_pill_cb,LV_EVENT_CLICKED,(void *)(intptr_t)1);
lv_obj_send_event(pill,LV_EVENT_CLICKED,NULL);arrive(80);
assert(!strcmp(list_current.b,"children")&&list_current.n==0&&!list_current.anchor.valid);
assert(lv_obj_get_scroll_y(svclist_obj(list))==0);
assert(!list_history_len||list_history[list_history_len-1].kind!=JOB_BROWSE_BOOKS);
lv_obj_delete(pill);
'''.replace('\n+','\n')
 body=body.replace('SERVICE_LIST',svc+'list_t')
 body+='''// Existing history remains bounded while walking many categories.
 for(int i=0;i<12;i++) {
 char id[32];snprintf(id,sizeof(id),"%d",i);
'''+call('Item','JOB_ARTIST','id','NULL',0)+'''
 arrive(80);
 }
 assert(list_history_len==LIST_HISTORY_DEPTH);
 // Stale page responses must not alter the current throttle/error cooldown.
 shown->list=(SERVICE_LIST *)1;page_asked=3;page_failed=7;page_failed_at=100;
 int before_binds=bind_calls;
 for(int old=0;old<4;old++) {
 page_result_t r={.list=old<2?(SERVICE_LIST *)2:shown->list,.generation=old<2?list_generation:list_generation-1,.page=3};
 r.error[0]=(old%2)?'x':0;page_callback(&r);
 assert(page_asked==3&&page_failed==7&&page_failed_at==100);
 assert(bind_calls==before_binds&&explicit_visible==0&&bars_updated==0);
 }
 assert(freed_pages==4);
 page_result_t current={.list=shown->list,.generation=list_generation,.page=3};
 page_callback(&current);assert(page_asked==-1&&explicit_visible==1&&bars_updated==1);
 page_asked=4;current.page=3;page_callback(&current);assert(page_asked==4);
 current.page=4;current.error[0]='x';page_callback(&current);
 assert(page_asked==-1&&page_failed==4&&page_failed_at>=100&&freed_pages==7);
 lv_obj_delete(svclist_obj(list));
 puts("PASS actual extracted '''+svc+''' history Back/search/artist/album/generation/bounded identities");return 0;
}
'''
 (out/f'{svc}-history-test.c').write_text(body.replace('SERVICE_LIST',svc+'list_t'))
