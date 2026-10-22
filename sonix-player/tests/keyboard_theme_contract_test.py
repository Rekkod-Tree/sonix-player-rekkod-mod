"""Focused keyboard contracts using actual production functions and explicit LVGL fixtures.

Run from sonix-player: python3 tests/keyboard_theme_contract_test.py
No rendering/device acceptance is claimed. CFLAGS supports sanitizer runs.
"""
from pathlib import Path
import argparse
import os
import re
import resource
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--case', choices=('all', 'tray', 'carets', 'labels'), default='all')
args = parser.parse_args()
base = Path(__file__).resolve().parents[1]
keyboard = (base / 'src/gui/shell/keyboard.c').read_text()
styles = (base / 'src/gui/shell/sx_styles.c').read_text()


def function(source, name):
    match = re.search(r'^(?:static )?[^;\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t lv_color_t;
typedef unsigned lv_style_selector_t;
typedef int lv_event_code_t;
typedef struct object lv_obj_t;
typedef struct event { lv_obj_t *target; void *user; } lv_event_t;
typedef void (*event_cb_t)(lv_event_t *);
enum { LV_PART_CURSOR=1,LV_STATE_FOCUSED=2,LV_OPA_TRANSP=0,LV_OPA_COVER=255,LV_BORDER_SIDE_LEFT=1,
       LV_STYLE_BG_COLOR=10,LV_STYLE_BG_OPA=11,LV_EVENT_DELETE=30 };
struct object {
 char text[40];lv_color_t border[2],bg,ink,text_ink;
 unsigned border_opa[2],width[2],side[2],background_opa[2],bg_opa;
 bool dead,shown,bound;unsigned blink;
 lv_obj_t *child;const void *image;
 event_cb_t deleted;void *deleted_user;unsigned delete_bindings;
};
typedef struct keyboard_s {
 lv_obj_t *tray,*letter_label[4],*mode_label,*shift_btn,*shift_icon,*accept_btn;
 lv_obj_t *t9_label[9],*t9_mode_label,*t9_shift_btn,*t9_shift_icon,*t9_accept_btn;
 uint8_t key_row[4],key_col[4];int key_count;
 bool shift,caps,symbols,symbols2,t9_numbers;
 struct keyboard_s *next;
} keyboard_t;
typedef struct palette { bool dark;lv_color_t accent,on_accent,text_primary,surface_pressed; } theme_palette_t;
static theme_palette_t palette;
static int family;
static unsigned text_writes,theme_bindings,allocations;
static bool fail_allocation,fail_event_binding;
static keyboard_t *kb_list;
static bool refresh_registered;
static int icon_shift,icon_caps_lock;
static theme_palette_t *theme(void) { return &palette; }
static int theme_get_palette(void) { return family; }
static lv_color_t lv_color_make(unsigned r,unsigned g,unsigned b) { return (r<<16)|(g<<8)|b; }
static void theme_register_refresh(void (*cb)(void)) { assert(cb);theme_bindings++; }
static unsigned part(lv_style_selector_t p) { return !!(p&LV_STATE_FOCUSED); }
static void valid(lv_obj_t *o) { assert(o&&!o->dead); }
static void lv_obj_set_style_bg_color(lv_obj_t *o,lv_color_t c,unsigned p) { (void)p;valid(o);o->bg=c; }
static void lv_obj_set_style_bg_opa(lv_obj_t *o,unsigned v,unsigned p) { valid(o);o->background_opa[part(p)]=v;o->bg_opa=v; }
static void lv_obj_set_style_image_recolor(lv_obj_t *o,lv_color_t c,unsigned p) { (void)p;valid(o);o->ink=c; }
static void lv_obj_set_style_image_recolor_opa(lv_obj_t *o,unsigned v,unsigned p) { (void)v;(void)p;valid(o); }
static void lv_obj_set_style_text_color(lv_obj_t *o,lv_color_t c,unsigned p) { (void)p;valid(o);o->text_ink=c; }
static lv_obj_t *lv_obj_get_child(lv_obj_t *o,int n) { assert(n==0);valid(o);return o->child; }
static void lv_obj_remove_local_style_prop(lv_obj_t *o,unsigned prop,unsigned p) {
 (void)p;valid(o);if(prop==LV_STYLE_BG_COLOR)o->bg=0;if(prop==LV_STYLE_BG_OPA)o->bg_opa=0;
}
static void lv_image_set_src(lv_obj_t *o,const void *image) { valid(o);o->image=image; }
static void lv_label_set_text(lv_obj_t *o,const char *s) { valid(o);snprintf(o->text,sizeof(o->text),"%s",s);text_writes++; }
static const char *lv_label_get_text(lv_obj_t *o) { valid(o);return o->text; }
static void lv_obj_set_style_border_color(lv_obj_t *o,lv_color_t c,unsigned p) { valid(o);o->border[part(p)]=c; }
static void lv_obj_set_style_border_width(lv_obj_t *o,unsigned v,unsigned p) { valid(o);o->width[part(p)]=v; }
static void lv_obj_set_style_border_side(lv_obj_t *o,unsigned v,unsigned p) { valid(o);o->side[part(p)]=v; }
static void lv_obj_set_style_border_opa(lv_obj_t *o,unsigned v,unsigned p) { valid(o);o->border_opa[part(p)]=v; }
static void kb_caret_blink_on(lv_obj_t *o) { valid(o);o->blink=900; }
static void kb_caret_blink_off(lv_obj_t *o) { valid(o);o->blink=0; }
static void onscreen_bind(lv_obj_t *o,void (*on)(lv_obj_t *),void (*off)(lv_obj_t *)) {
 valid(o);if(!o->bound){o->bound=true;if(o->shown)on(o);else off(o);}
}
static void *lv_obj_add_event_cb(lv_obj_t *o,event_cb_t cb,lv_event_code_t code,void *user) {
 valid(o);assert(code==LV_EVENT_DELETE);if(fail_event_binding)return NULL;
 o->deleted=cb;o->deleted_user=user;o->delete_bindings++;return o;
}
static lv_obj_t *lv_event_get_target(lv_event_t *e) { return e->target; }
static void *fixture_malloc(size_t size) { allocations++;return fail_allocation?NULL:malloc(size); }
static const char *kb_key_text(const keyboard_t *kb,int row,int col) {
 (void)row;(void)col;return kb->symbols?(kb->symbols2?"@":"1"):(kb->shift?"A":"a");
}
static int kb_t9_len(const keyboard_t *kb,int key) { (void)kb;(void)key;return 1; }
static const char *kb_t9_step(const keyboard_t *kb,int key,int step) { (void)key;(void)step;return kb->shift?"A":"a"; }
#define KB_T9_KEYS 9
#define KB_CARET_WIDTH 3
'''
# Keep the actual fixed tables and the current caret registry declaration.
arrays = ''
for name in ('KB_T9_NUM', 'KB_T9_NUM2'):
    match = re.search(r'^static const char \*const ' + name + r'\[[^]]+\] = \{', keyboard, re.MULTILINE)
    assert match
    arrays += keyboard[match.start():keyboard.index('};', match.end()) + 2] + '\n'
state = keyboard[keyboard.index('// Every textarea whose caret'):keyboard.index('static void kb_refresh_theme(void);')]
code = prefix + arrays + state
code += re.search(r'^static const lv_style_selector_t kb_caret_parts\[\] = [^;]+;', keyboard, re.MULTILINE).group() + '\n'
code += 'static void kb_refresh_theme(void);\n'
code += function(styles, 'sx_set_text')
code += '#define malloc fixture_malloc\n'
for name in ('kb_register_refresh', 'kb_tray_color', 'kb_refresh_caps'):
    code += function(keyboard, name)
for name in ('kb_caret_field_deleted', 'kb_refresh_caret_color'):
    if re.search(r'^(?:static )?[^;\n]+\b' + name + r'\([^;]*?\)\s*\{', keyboard, re.MULTILINE):
        code += function(keyboard, name)
code += 'void keyboard_style_caret(lv_obj_t *field);\n'
code += function(keyboard, 'kb_refresh_theme')
code += function(keyboard, 'keyboard_style_caret')
code += '#undef malloc\n'
code += r'''
static void destroy_field(lv_obj_t *o) {
 assert(!o->dead);if(o->deleted){lv_event_t e={.target=o,.user=o->deleted_user};o->deleted(&e);}o->dead=true;
}
static void setup_keyboard(keyboard_t *kb,lv_obj_t o[24]) {
 memset(kb,0,sizeof(*kb));memset(o,0,24*sizeof(*o));
 kb->tray=&o[0];kb->key_count=2;kb->letter_label[0]=&o[1];kb->letter_label[1]=&o[2];
 kb->mode_label=&o[3];kb->shift_btn=&o[4];kb->shift_icon=&o[5];
 kb->accept_btn=&o[6];o[6].child=&o[7];
 for(int i=0;i<KB_T9_KEYS;i++)kb->t9_label[i]=&o[8+i];
 kb->t9_mode_label=&o[17];kb->t9_shift_btn=&o[18];kb->t9_shift_icon=&o[19];
 kb->t9_accept_btn=&o[20];o[20].child=&o[21];
 kb_refresh_caps(kb);
}
static void test_tray(void) {
 family=0;palette.dark=true;assert(kb_tray_color()==0x161616);
 palette.dark=false;assert(kb_tray_color()==0xced1d8);
 for(int p=1;p<=8;p++)for(int dark=0;dark<2;dark++) {
  family=p;palette.dark=dark;palette.surface_pressed=0x203040+(unsigned)p*0x1000+dark;
  assert(kb_tray_color()==palette.surface_pressed);
 }
 puts("PASS palette0 exact tray RGB and role-based tray for all new families/light/dark");
}
static void test_carets(void) {
 lv_obj_t fields[40]={0};palette.accent=0x102030;
 for(int i=0;i<40;i++){fields[i].shown=i%2;keyboard_style_caret(&fields[i]);}
 assert(theme_bindings==1);
 unsigned before=fields[5].delete_bindings;keyboard_style_caret(&fields[5]);
 assert(fields[5].delete_bindings==before&&before==1);
 fields[4].border_opa[0]=fields[4].border_opa[1]=0;
 palette.accent=0x789abc;kb_refresh_theme();
 for(int i=0;i<40;i++) {
  assert(fields[i].border[0]==palette.accent&&fields[i].border[1]==palette.accent);
  assert(fields[i].width[0]==3&&fields[i].width[1]==3);
  assert(fields[i].blink==(fields[i].shown?900U:0U));
 }
 assert(!fields[4].border_opa[0]&&!fields[4].border_opa[1]);
 destroy_field(&fields[3]);palette.accent=0xaabbcc;kb_refresh_theme();
 for(int i=0;i<40;i++)if(i!=3){assert(fields[i].border[0]==palette.accent);destroy_field(&fields[i]);}
 kb_refresh_theme();
 lv_obj_t retry={0};fail_allocation=true;keyboard_style_caret(&retry);
 assert(retry.border[0]==palette.accent&&!retry.deleted);
 fail_allocation=false;keyboard_style_caret(&retry);assert(retry.deleted&&retry.delete_bindings==1);
 destroy_field(&retry);kb_refresh_theme();
 lv_obj_t event_retry={0};fail_event_binding=true;keyboard_style_caret(&event_retry);
 assert(event_retry.border[0]==palette.accent&&!event_retry.deleted);
 fail_event_binding=false;keyboard_style_caret(&event_retry);assert(event_retry.delete_bindings==1);
 destroy_field(&event_retry);kb_refresh_theme();
 puts("PASS 40 carets, both selectors, no duplicates, deletion, hidden blink/opacity and OOM retry");
}
static void test_labels(void) {
 keyboard_t first,second;lv_obj_t one[24],two[24];
 palette.accent=0x123456;palette.on_accent=0xffffff;palette.text_primary=0xe0e0e0;
 setup_keyboard(&first,one);setup_keyboard(&second,two);first.next=&second;kb_list=&first;
 unsigned initial=text_writes;assert(initial>0);text_writes=0;
 palette.accent=0x765432;palette.on_accent=0x101010;kb_refresh_theme();
 assert(text_writes==0);
 assert(one[6].bg==palette.accent&&two[20].bg==palette.accent);
 assert(one[7].text_ink==palette.on_accent&&two[21].ink==palette.on_accent);
 first.shift=true;first.caps=true;kb_refresh_caps(&first);
 assert(!strcmp(one[1].text,"A")&&one[5].image==&icon_caps_lock);
 assert(one[4].bg==palette.accent&&one[5].ink==palette.on_accent&&text_writes>0);
 first.symbols=true;first.symbols2=true;first.t9_numbers=true;kb_refresh_caps(&first);
 assert(!strcmp(one[1].text,"@")&&!strcmp(one[3].text,"abc"));
 assert(!strcmp(one[8].text,"1\n[]")&&!strcmp(one[17].text,"abc"));
 text_writes=0;palette.accent=0x010203;kb_refresh_theme();assert(text_writes==0);
 assert(one[4].bg==palette.accent&&one[18].bg==palette.accent);
 kb_list=NULL;
 puts("PASS theme refresh has zero LVGL text resets; QWERTY/T9 Shift/Caps/symbol labels still change");
}
int main(int argc,char **argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"tray"))test_tray();
 else if(!strcmp(argv[1],"carets"))test_carets();
 else if(!strcmp(argv[1],"labels"))test_labels();
 else assert(!"Unknown case");
 return 0;
}
'''
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix='sonix-keyboard-theme-') as temporary:
    folder = Path(temporary)
    fixture = folder / 'keyboard_theme_contract.c'
    fixture.write_text(code)
    binary = folder / 'keyboard_theme_contract'
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-function', '-Wno-unused-variable']
    command += shlex.split(os.environ.get('CFLAGS', ''))
    subprocess.run(command + [str(fixture), '-o', str(binary)], check=True)
    failed = []
    for case in ('tray', 'carets', 'labels') if args.case == 'all' else (args.case,):
        process = subprocess.run([str(binary), case])
        if process.returncode:
            failed.append(case)
    if failed:
        raise SystemExit('FAIL keyboard contract modes: ' + ', '.join(failed))
