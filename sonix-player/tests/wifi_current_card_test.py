"""Actual production Wi-Fi page callbacks with inert LVGL/backend fixtures.

Run from sonix-player under Linux/WSL: python3 tests/wifi_current_card_test.py
No output artifacts, baseline copies, device, radio or network are required.
Negative controls must compile, then fail at the intended runtime assertion.
This checks coordination/content; synthetic query/caret sentinels are not a
physical rendering or keyboard-focus acceptance test.
"""
from pathlib import Path
import os
import re
import resource
import shlex
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
production = project / 'src/gui/wireless/wifisettings.c'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def function(source, name):
    match = re.search(r'^static [A-Za-z_][A-Za-z0-9_]*[ \t]+' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.MULTILINE)
    require(match is not None, 'Missing production callback: ' + name)
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
#include <string.h>
#include "src/system/net/wifi.h"
typedef struct object {bool hidden;unsigned state;int scroll;char text[160];} lv_obj_t;
typedef struct timer {bool ready,paused;} lv_timer_t;
typedef struct event {int unused;} lv_event_t;
enum {LV_STATE_CHECKED=1,LV_STATE_DISABLED=2,LV_OPA_20=51,LV_OPA_50=127,LV_OPA_COVER=255};
static lv_obj_t card_object,switch_object,status_object,list_object,section_object,scan_object,scan_image;
static lv_obj_t *current_card=&card_object,*wifi_switch=&switch_object,*status_label=&status_object,*list=&list_object,*section_label=&section_object,*scan_btn=&scan_object,*scan_icon=&scan_image;
static lv_timer_t timer_object,*poll_timer=&timer_object;
static char current_ssid[WIFI_SSID_MAX],card_title[80],card_value[80],card_subtitle[160];
static wifi_status_t drawn_current_status,status;
static bool drawn_current_valid,drawn_current_shown;
static uint32_t drawn_serial=0xffffffffU,serial=7;
static bool drawn_enabled,drawn_scanning,scanned_since_on=true,on=true,available=true,busy,scanning;
static wifi_network_t networks[3],rows[3];
static int row_count,clean_count,title_updates,value_updates,subtitle_updates,scan_requests,reads;
static char unrelated_query[32]="unchanged query";static int unrelated_caret=4;
static const char *tr(const char *key) {
 if(!strcmp(key,"wifi_dbm"))return "%d dBm";
 if(!strcmp(key,"wifi_mbps"))return "%d Mbit/s";
 return key;
}
bool wifi_get_enabled(void){return on;}
bool wifi_available(void){return available;}
bool wifi_busy(void){return busy;}
bool wifi_scan_running(void){return scanning;}
void wifi_scan_start(void){scan_requests++;scanning=true;}
uint32_t wifi_networks_serial(void){return serial;}
void wifi_get_status(wifi_status_t *out){*out=status;reads++;}
int wifi_get_networks(wifi_network_t *out,int max){assert(max>=3);memcpy(out,networks,sizeof(networks));return 3;}
wifi_op_t wifi_take_op_result(char *ssid,int size){assert(size>0);ssid[0]=0;return WIFI_OP_IDLE;}
wifi_opkind_t wifi_last_op_kind(void){return WIFI_OPKIND_CONNECT;}
wifi_fail_t wifi_last_failure(void){return WIFI_FAIL_NONE;}
static bool lv_obj_has_state(lv_obj_t *obj,unsigned state){return (obj->state&state)!=0;}
static void lv_obj_add_state(lv_obj_t *obj,unsigned state){obj->state|=state;}
static void lv_obj_remove_state(lv_obj_t *obj,unsigned state){obj->state&=~state;}
static void lv_obj_set_hidden(lv_obj_t *obj,bool hidden){obj->hidden=hidden;}
static void lv_obj_set_style_image_opa(lv_obj_t *obj,int opacity,int part){(void)obj;(void)opacity;(void)part;}
static void lv_label_set_text(lv_obj_t *obj,const char *text){snprintf(obj->text,sizeof(obj->text),"%s",text);}
static void lv_obj_clean(lv_obj_t *obj){assert(obj==list);clean_count++;row_count=0;obj->scroll=0;}
static lv_obj_t *sx_card_title(lv_obj_t *card,const char *text){assert(card==current_card);title_updates++;snprintf(card_title,sizeof(card_title),"%s",text);return card;}
static lv_obj_t *sx_card_value(lv_obj_t *card,const char *text){assert(card==current_card);value_updates++;snprintf(card_value,sizeof(card_value),"%s",text);return card;}
static lv_obj_t *sx_card_subtitle(lv_obj_t *card,const char *text,bool mono){assert(card==current_card&&mono);subtitle_updates++;snprintf(card_subtitle,sizeof(card_subtitle),"%s",text);return card;}
static void add_row(lv_obj_t *parent,const wifi_network_t *network){assert(parent==list&&row_count<3);rows[row_count++]=*network;}
static void add_placeholder(const char *key,bool spin){(void)key;(void)spin;}
static void toast_plain(const char *key){(void)key;assert(!"Unexpected toast");}
static void toast_success(const char *key){(void)key;assert(!"Unexpected toast");}
static void toast_error(const char *key){(void)key;assert(!"Unexpected toast");}
static void topbar_refresh_radios(void){assert(!"Unexpected result update");}
static void ask_for_password(const char *ssid){(void)ssid;assert(!"Unexpected password prompt");}
static void lv_timer_resume(lv_timer_t *timer){timer->paused=false;}
static void lv_timer_ready(lv_timer_t *timer){timer->ready=true;}
'''

tail = r'''
static bool has_row(const char *ssid,bool expected_current) {
 for(int i=0;i<row_count;i++)if(!strcmp(rows[i].ssid,ssid)){assert(rows[i].current==expected_current);return true;}
 return false;
}
static void reset(void) {
 memset(&status,0,sizeof(status));memset(&drawn_current_status,0,sizeof(drawn_current_status));
 drawn_current_valid=drawn_current_shown=false;current_ssid[0]=card_title[0]=card_value[0]=card_subtitle[0]=0;
 card_object.hidden=true;switch_object.state=0;list_object.scroll=0;
 drawn_serial=0xffffffffU;drawn_enabled=drawn_scanning=false;
 scanned_since_on=true;on=available=true;busy=scanning=false;serial=7;
 row_count=clean_count=title_updates=value_updates=subtitle_updates=scan_requests=reads=0;
 for(int i=0;i<3;i++){memset(&networks[i],0,sizeof(networks[i]));snprintf(networks[i].ssid,sizeof(networks[i].ssid),"AP-%c",'A'+i);networks[i].saved=true;}
 status.state=WIFI_STATE_ON;
}
static void connected(const char *ssid) {
 status.state=WIFI_STATE_CONNECTED;snprintf(status.ssid,sizeof(status.ssid),"%s",ssid);
 snprintf(status.ip,sizeof(status.ip),"192.0.2.7");status.signal=-52;status.freq_mhz=5180;status.link_mbps=144;
}
static void poll(void){poll_cb(NULL);assert(serial==7&&!strcmp(unrelated_query,"unchanged query")&&unrelated_caret==4);}
int main(void) {
 reset();on=false;status.state=WIFI_STATE_OFF;poll();
 on=true;busy=true;status.state=WIFI_STATE_ON;poll();
 busy=false;poll();assert(scanning&&scanned_since_on);
 scanning=false;poll();assert(current_card->hidden);
 int cleans=clean_count;connected("AP-A");poll();
 assert(!current_card->hidden&&!strcmp(current_ssid,"AP-A")&&!strcmp(card_value,"wifi_network_connected"));
 assert(clean_count==cleans+1&&!has_row("AP-A",false)&&has_row("AP-B",false));
 cleans=clean_count;list->scroll=123;int titles=title_updates,values=value_updates,subtitles=subtitle_updates;
 for(int i=0;i<100;i++){status.signal=-40-i%30;poll();}
 assert(clean_count==cleans&&list->scroll==123&&title_updates==titles&&value_updates==values&&subtitle_updates==subtitles+100);
 assert(strstr(card_subtitle,"-49 dBm"));
 subtitles=subtitle_updates;reads=0;
 for(int i=0;i<30;i++){status.bars=i%4;snprintf(status.ip,sizeof(status.ip),"192.0.2.%d",i+1);poll();}
 assert(clean_count==cleans&&list->scroll==123&&title_updates==titles&&value_updates==values&&subtitle_updates==subtitles);
 for(int i=0;i<100;i++)poll();
 assert(clean_count==cleans&&subtitle_updates==subtitles);
 status.state=WIFI_STATE_CONNECTING;poll();
 assert(clean_count==cleans&&list->scroll==123&&!strcmp(card_value,"connecting")&&!card_subtitle[0]);
 connected("AP-A");poll();assert(clean_count==cleans&&list->scroll==123&&!strcmp(card_value,"wifi_network_connected"));
 /* Late SSID and a different current identity work without backend serial changes. */
 status.state=WIFI_STATE_CONNECTING;status.ssid[0]=0;poll();assert(current_card->hidden);
 snprintf(status.ssid,sizeof(status.ssid),"AP-B");poll();
 assert(!current_card->hidden&&!strcmp(current_ssid,"AP-B")&&!strcmp(card_title,"AP-B")&&!strcmp(card_value,"connecting"));
 cleans=clean_count;connected("AP-B");poll();assert(clean_count==cleans&&!strcmp(card_value,"wifi_network_connected"));
 /* Disconnect/hide must restore even a stale scan-cache current entry as an ordinary row. */
 networks[1].current=true;status.state=WIFI_STATE_ON;status.ssid[0]=0;poll();
 assert(current_card->hidden&&!current_ssid[0]&&has_row("AP-B",false));
 connected("AP-C");poll();assert(!strcmp(current_ssid,"AP-C")&&has_row("AP-B",false)&&!has_row("AP-C",false));
 /* Availability and off/on clear visibility coherently; no query/caret side effects. */
 available=false;poll();assert(current_card->hidden);available=true;poll();assert(!current_card->hidden);
 on=false;poll();assert(current_card->hidden&&list->hidden);
 on=true;scanned_since_on=true;poll();assert(!current_card->hidden&&!list->hidden&&!strcmp(card_title,"AP-C"));
 /* A structural redraw refreshes localized text even if status itself is identical. */
 titles=title_updates;values=value_updates;rebuild_list();assert(title_updates==titles+1&&value_updates==values+1);
 screen_loaded_cb(NULL);scanning=false;poll();assert(!current_card->hidden&&!strcmp(current_ssid,"AP-C"));
 puts("PASS actual production callbacks: late state/SSID, connection/disconnection, authoritative identity, stale scan flags, RSSI-only in-place update, stable scroll/query/caret, no backend serial changes");
 return 0;
}
'''

content = production.read_text()
source = prefix
for name in ('searching_now', 'refresh_status_label', 'link_line', 'status_has_current',
             'current_status_changed', 'refresh_current_card', 'rebuild_list',
             'radio_ready', 'poll_cb', 'screen_loaded_cb'):
    source += function(content, name)
source += tail

# Alter only generated test code. Production source is never mutated.
identity = 'bool identity_changed = strcmp(current_ssid, identity) != 0;'
refresh = 'else if (current_status_changed(&status))'
require(source.count(identity) == 1 and source.count(refresh) == 1,
        'Live-refresh control points changed; re-review the regression control')
no_live_refresh = source.replace(identity, '(void)identity; bool identity_changed = false;')
no_live_refresh = no_live_refresh.replace(refresh, 'else if (false && current_status_changed(&status))')
stale_filter = 'if (current_ssid[0] && strcmp(networks[i].ssid, current_ssid) == 0)'
normalize = 'networks[i].current = false;'
require(source.count(stale_filter) == 1 and source.count(normalize) == 1,
        'Membership control points changed; re-review the regression control')
stale_authority = source.replace(stale_filter, 'if (networks[i].current || (current_ssid[0] && strcmp(networks[i].ssid, current_ssid) == 0))')
stale_authority = stale_authority.replace(normalize, '/* Negative control: stale scan flag is left authoritative. */')

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
variants = (
    ('production', source, None),
    ('disable-live-status-refresh', no_live_refresh, '!current_card->hidden'),
    ('restore-stale-scan-authority', stale_authority, 'current_card->hidden&&!current_ssid[0]&&has_row'),
)
with tempfile.TemporaryDirectory(prefix='sonix-wifi-current-card-') as temporary:
    folder = Path(temporary)
    for name, code, expected_assertion in variants:
        generated, binary = folder / (name + '.c'), folder / name
        generated.write_text(code)
        command = shlex.split(os.environ.get('CC', 'cc'))
        command += ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g', '-I' + str(project)]
        command += shlex.split(os.environ.get('CFLAGS', ''))
        # A compiler failure is never accepted as a successful negative control.
        subprocess.run(command + [str(generated), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        if expected_assertion is None:
            require(result.returncode == 0, 'Production callback regression failed:\n' + result.stderr)
            print(result.stdout.strip())
        else:
            require(result.returncode != 0 and 'Assertion' in result.stderr and expected_assertion in result.stderr,
                    'Negative control did not fail at its intended runtime assertion: ' + name + '\n' + result.stderr)
            print('PASS runtime negative control: ' + name)
