"""Compile the actual source picker and navigation functions with UI/auth fixtures.

Run from sonix-player: python3 tests/search_scope_navigation_test.py
This checks coordination/history, not LVGL rendering or real service login.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]
menu = (base / 'src/gui/shell/main_menu.c').read_text()
switcher = (base / 'src/gui/shell/switcher.c').read_text()


def function(source, name):
    match = re.search(r'^(?:static )?[^;\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


enum = re.search(r'typedef enum \{[^}]+\} home_item_t;', (base / 'src/gui/shell/main_menu.h').read_text()).group()
state = menu[menu.index('static int last_search_scope'):menu.index('static const char *ids[]')]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct object { void *user; char query[80], label[40]; int cursor; bool focused; } lv_obj_t;
typedef struct event { void *user; } lv_event_t;
typedef struct image { int unused; } lv_image_dsc_t;
typedef struct item { const char *label; const lv_image_dsc_t *icon; void (*action)(void *); void *user; } sx_sheet_item_t;
typedef enum { NAV_NONE, NAV_FORWARD, NAV_BACK } nav_dir_t;
enum { LV_EVENT_SIZE_CHANGED };
static lv_obj_t home,more,music_input,ym_input,zv_input,ym_home,player,browser;
static lv_obj_t *main_menu_screen=&home,*search_screen=&music_input,*yandex_screen=&ym_home;
static lv_obj_t *player_screen=&player,*browser_screen=&browser,*active=&home,*player_return_screen;
#ifdef SONIX_ZVUK
static lv_obj_t zv_home;
static lv_obj_t *zvuk_screen=&zv_home;
#endif
static lv_obj_t *screen_history[8];
static int screen_history_len;
#define SCREEN_HISTORY_DEPTH 8
static bool ym_auth=true,ym_available=true,zv_available=true,sheet_open,player_open;
#ifdef SONIX_ZVUK
static bool zv_auth=true;
#endif
static sx_sheet_item_t shown_items[3];
static int shown_count,load_count;
static lv_image_dsc_t icon_menu_music;
static void gui_init_rest(void) {}
static lv_obj_t *lv_screen_active(void) { return active; }
static bool yandex_logged_in(void) { return ym_auth; }
#ifdef SONIX_ZVUK
static bool zvuk_logged_in(void) { return zv_auth; }
#endif
static const char *tr(const char *s) { return s; }
static void *lv_event_get_user_data(lv_event_t *e) { return e->user; }
static void *lv_obj_get_user_data(lv_obj_t *o) { return o->user; }
static void lv_label_set_text(lv_obj_t *o,const char *text) { snprintf(o->label,sizeof(o->label),"%s",text); }
static void lv_obj_send_event(lv_obj_t *o,int event,void *data) { (void)o;(void)event;(void)data; }
static void sx_sheet_menu(const char *title,const char *subtitle,const sx_sheet_item_t *items,int count) {
 (void)title;(void)subtitle;assert(count<=3);memcpy(shown_items,items,(size_t)count*sizeof(*items));shown_count=count;sheet_open=true;
}
static bool player_sheet_is_open(void) { return player_open; }
static void player_sheet_open(bool animate) { (void)animate;player_open=true; }
static void player_sheet_close(bool animate) { (void)animate;player_open=false; }
static bool (*active_back_guard(void))(void) { return NULL; }
static bool browser_go_up(void) { return false; }
static void load_screen_dir(lv_obj_t *,nav_dir_t);
static void load_screen(lv_obj_t *target_screen);
void switch_screen(lv_obj_t *);
void switch_screen_no_history(lv_obj_t *);
static void search_scope_loaded(lv_event_t *);
static void search_scope_choose(void);
void yandexpage_search_open(void);
void search_open(void);
#ifdef SONIX_ZVUK
void zvukpage_search_open(void);
#endif
'''
after_enum = r'''
static bool main_menu_available(home_item_t source) {
 if(source==HOME_MUSIC)return true;
 if(source==HOME_YANDEX)return ym_available;
#ifdef SONIX_ZVUK
 if(source==HOME_ZVUK)return zv_available;
#endif
 return false;
}
static const lv_image_dsc_t *main_menu_icon(home_item_t source) { (void)source;return &icon_menu_music; }
'''
source = prefix + enum + '\n' + state + after_enum
for name in ('history_push', 'history_pop', 'load_screen', 'back_goes_to_player', 'switch_screen_no_history', 'switch_screen', 'back_btn_cb'):
    source += function(switcher, name)
for name in ('search_scope_register', 'search_scope_screen', 'search_scope_is_input', 'search_scope_action',
             'search_scope_choose', 'main_menu_open_search', 'search_scope_tag', 'search_scope_loaded'):
    source += function(menu, name)
source += function((base / 'src/gui/library/search.c').read_text(), 'search_open')
# Each provider has a different translation-unit-local search_screen symbol.
source += re.sub(r'\bsearch_screen\b', '(&ym_input)', function((base / 'src/gui/streaming/yandexpage.c').read_text(), 'yandexpage_search_open'))
source += '#ifdef SONIX_ZVUK\n' + re.sub(r'\bsearch_screen\b', '(&zv_input)', function((base / 'src/gui/streaming/zvukpage.c').read_text(), 'zvukpage_search_open')) + '#endif\n'
source += r'''
static void load_screen_dir(lv_obj_t *target,nav_dir_t direction) {
 (void)direction;assert(target);active=target;load_count++;
 for(int i=0;i<search_input_count;i++)if(search_inputs[i].screen==target) {
  lv_obj_t title={.user=(void *)(intptr_t)search_inputs[i].source};lv_event_t event={.user=&title};search_scope_loaded(&event);break;
 }
}
static void choose(home_item_t source) {
 search_scope_choose();assert(sheet_open);
 for(int i=0;i<shown_count;i++)if((home_item_t)(intptr_t)shown_items[i].user==source) {
  sx_sheet_item_t item=shown_items[i];sheet_open=false;item.action(item.user);return;
 }
 assert(!"Requested source not offered");
}
static void reset(lv_obj_t *opener) {
 active=opener;screen_history_len=0;last_search_scope=-1;sheet_open=false;player_open=false;
 player_return_screen=NULL;ym_auth=true;ym_available=true;zv_available=true;load_count=0;
#ifdef SONIX_ZVUK
 zv_auth=true;
#endif
 strcpy(music_input.query,"card query");music_input.cursor=4;music_input.focused=true;
 strcpy(ym_input.query,"yandex query");ym_input.cursor=5;ym_input.focused=true;
 strcpy(zv_input.query,"zvuk query");zv_input.cursor=2;zv_input.focused=false;
}
static void unchanged_fields(void) {
 assert(!strcmp(music_input.query,"card query")&&music_input.cursor==4&&music_input.focused);
 assert(!strcmp(ym_input.query,"yandex query")&&ym_input.cursor==5&&ym_input.focused);
 assert(!strcmp(zv_input.query,"zvuk query")&&zv_input.cursor==2&&!zv_input.focused);
}
static void first_global(home_item_t source) {
 main_menu_open_search();assert(sheet_open);choose(source);assert(!sheet_open);
}
int main(void) {
 search_scope_register(&music_input,HOME_MUSIC);search_scope_register(&ym_input,HOME_YANDEX);
#ifdef SONIX_ZVUK
 search_scope_register(&zv_input,HOME_ZVUK);
#endif
 for(int origin=0;origin<2;origin++) {
  lv_obj_t *opener=origin?&more:&home;
  reset(opener);first_global(HOME_YANDEX);assert(active==&ym_input);
  assert(screen_history_len==1&&screen_history[0]==opener);
  choose(HOME_MUSIC);assert(active==&music_input&&last_search_scope==HOME_MUSIC&&screen_history_len==1);
  back_btn_cb(NULL);assert(active==opener&&screen_history_len==0&&last_search_scope==HOME_MUSIC);
  main_menu_open_search();assert(active==&music_input&&!sheet_open&&screen_history_len==1);
  unchanged_fields();back_btn_cb(NULL);assert(active==opener);
  reset(opener);first_global(HOME_MUSIC);choose(HOME_YANDEX);assert(active==&ym_input);
  back_btn_cb(NULL);assert(active==opener&&last_search_scope==HOME_YANDEX);
  main_menu_open_search();assert(active==&ym_input&&!sheet_open);unchanged_fields();
  for(int i=0;i<12;i++) {
   choose(HOME_MUSIC);
#ifdef SONIX_ZVUK
   choose(HOME_ZVUK);assert(active==&zv_input&&screen_history_len==1);
#endif
   choose(HOME_YANDEX);assert(screen_history_len==1&&screen_history[0]==opener);
  }
  choose(HOME_MUSIC);back_btn_cb(NULL);assert(active==opener&&last_search_scope==HOME_MUSIC);
 }
 /* Cancel and choosing the same source preserve fields and history exactly. */
 reset(&more);first_global(HOME_MUSIC);int loads=load_count;
 search_scope_choose();assert(sheet_open);sheet_open=false;
 assert(active==&music_input&&screen_history_len==1&&last_search_scope==HOME_MUSIC&&load_count==loads);unchanged_fields();
 choose(HOME_MUSIC);assert(load_count==loads&&screen_history_len==1);unchanged_fields();
 /* Direct service search keeps its service opener, even after switching input. */
 reset(&ym_home);yandexpage_search_open();assert(active==&ym_input&&screen_history[0]==&ym_home);
 choose(HOME_MUSIC);back_btn_cb(NULL);assert(active==&ym_home&&screen_history_len==0);unchanged_fields();
#ifdef SONIX_ZVUK
 reset(&zv_home);zvukpage_search_open();choose(HOME_YANDEX);choose(HOME_MUSIC);
 back_btn_cb(NULL);assert(active==&zv_home&&screen_history_len==0);unchanged_fields();
#endif
 /* A destination already in deeper legitimate history must not pop the opener. */
 reset(&home);search_open();switch_screen(&ym_home);yandexpage_search_open();
 assert(screen_history_len==3&&screen_history[2]==&ym_home);
 choose(HOME_MUSIC);assert(screen_history_len==3&&screen_history[2]==&ym_home);
 back_btn_cb(NULL);assert(active==&ym_home);back_btn_cb(NULL);assert(active==&music_input);back_btn_cb(NULL);assert(active==&home);
 /* Auth fallback uses the existing provider opener; no login bypass. */
 reset(&more);first_global(HOME_MUSIC);ym_auth=false;choose(HOME_YANDEX);
 assert(active==&ym_home&&screen_history_len==2&&screen_history[1]==&music_input);
 back_btn_cb(NULL);assert(active==&music_input&&last_search_scope==HOME_MUSIC);unchanged_fields();
#ifdef SONIX_ZVUK
 zv_auth=false;choose(HOME_ZVUK);assert(active==&zv_home&&screen_history_len==2);
 back_btn_cb(NULL);assert(active==&music_input&&last_search_scope==HOME_MUSIC);
#endif
 /* A build without Zvuk and an unavailable YM never offer those destinations. */
 ym_available=false;zv_available=false;search_scope_choose();assert(shown_count==1);
 int history=screen_history_len;lv_obj_t *before=active;
 search_scope_action((void *)(intptr_t)HOME_YANDEX);
 search_scope_action((void *)(intptr_t)HOME_ZVUK);
 assert(active==before&&screen_history_len==history);unchanged_fields();
 puts("PASS search source replacement: opener/back/reentry, both directions, repeated switches, direct entry, cancel, auth, existing deeper history");
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='sonix-search-scope-') as temporary:
    folder = Path(temporary)
    fixture = folder / 'search_scope_navigation.c'
    fixture.write_text(source)
    for zvuk in (False, True):
        binary = folder / ('with-zvuk' if zvuk else 'without-zvuk')
        command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g']
        command += shlex.split(os.environ.get('CFLAGS', ''))
        if zvuk:
            command += ['-DSONIX_ZVUK=1']
        command += [str(fixture), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
        print('PASS with Zvuk' if zvuk else 'PASS without Zvuk')
