"""Actual UI state callbacks with inert radio/GUI fixtures. Run from any cwd.
Optional --reviewed-overlay is for testing an unapplied review copy only.
No output baseline, device, network, credentials or production writes.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import sys
import argparse
import resource
def function(text, name):
    import re
    match = re.search(r'^(?:static )?(?:void|bool|int) ' + re.escape(name) + r'\([^;]*?\)\s*\{', text, re.M)
    if not match:
        raise RuntimeError('Missing actual function: ' + name)
    start = match.start()
    pos = text.index('{', match.start())
    # All selected callbacks have balanced braces in their comments/strings.
    depth = 1
    pos += 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]

COMMON='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n'
BT_PREFIX='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n\n#include <pthread.h>\ntypedef void lv_timer_t;\ntypedef int lv_obj_t;\n#define BT_NAME_MAX 128\n#define SCAN_SECONDS 10\n#define LV_STATE_CHECKED 1\n#define LV_STATE_DISABLED 2\n#define LV_OPA_20 20\n#define LV_OPA_50 50\n#define LV_OPA_COVER 255\n#define ACTION_NONE 0\n#define ACTION_CONNECT 1\n#define ACTION_DISCONNECT 2\n#define ACTION_FORGET 3\n#define BT_OP_OK 1\n#define BT_OP_FAILED 2\nstatic pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;\nstatic bool g_scanning, g_enabled = true;\nstatic int g_seen_count, g_paired_count, g_found_count, g_hidden_count;\nstatic uint32_t drawn_serial = 7;\nstatic bool drawn_enabled = true, drawn_scanning;\nstatic lv_obj_t *bt_switch, *scan_btn, *scan_icon, *receiver_card;\nstatic int last_action, list_rebuilds;\nstatic bool spinner_visible;\nstatic bool bluetooth_get_enabled(void) { return g_enabled; }\nstatic bool bluetooth_scan_running(void) { return g_scanning; }\nstatic bool lv_obj_has_state(lv_obj_t *o, int s) { (void)o; (void)s; return true; }\nstatic void lv_obj_add_state(lv_obj_t *o, int s) { (void)o; (void)s; }\nstatic void lv_obj_remove_state(lv_obj_t *o, int s) { (void)o; (void)s; }\nstatic void refresh_status_label(void) {}\nstatic int bluetooth_headset_battery(void *p) { (void)p; return -1; }\nstatic void bluetooth_codec_summary(void *p,char *s,size_t n) { (void)p; snprintf(s,n,""); }\nstatic uint32_t bluetooth_devices_serial(void) { return 7; }\n\nstatic void refresh_codec_cards(void) {}\nstatic bool radio_ready(void) { return true; }\nstatic void lv_obj_set_style_image_opa(lv_obj_t *o,int x,int y) { (void)o;(void)x;(void)y; }\nstatic void sx_card_set_dim(lv_obj_t *o,bool x) { (void)o;(void)x; }\nstatic bool pairing_watch_active(void) { return false; }\nstatic int bluetooth_take_op_result(char *o,size_t s) { (void)o;(void)s;return 0; }\nstatic void toast_success(const char *s) { (void)s; }\nstatic void topbar_refresh_radios(void) {}\nstatic void gui_notify_popup(const char *s) { (void)s; }\nstatic bool btstack_discovery(bool on) { (void)on;return false; }\nstatic void sleep_ms(int n) { (void)n; }\nstatic void refresh_devices(void) {}\nstatic void apply_renames(void) {}\n\ntypedef struct { bool connected; } bt_device_t;\n#define BT_MAX_DEVICES 32\nstatic lv_obj_t connected_obj, paired_obj, found_obj;\nstatic lv_obj_t *connected_list=&connected_obj,*paired_list=&paired_obj,*found_list=&found_obj;\nstatic lv_obj_t *more_card,*paired_label,*found_label;\nstatic bool bluetooth_available(void) { return true; }\nstatic void lv_obj_clean(lv_obj_t *p) { if(p==found_list) { list_rebuilds++;spinner_visible=false; } }\nstatic void lv_obj_set_hidden(lv_obj_t *p,bool hidden) { if(p==found_list && hidden) spinner_visible=false; }\nstatic int bluetooth_get_paired(bt_device_t *d,int n) { (void)d;(void)n;return 0; }\nstatic int bluetooth_get_found(bt_device_t *d,int n) { (void)d;(void)n;return 0; }\nstatic void add_connected(bt_device_t *d) { (void)d; }\nstatic void add_paired(bt_device_t *d) { (void)d; }\nstatic void add_found(bt_device_t *d) { (void)d; }\nstatic void add_placeholder(lv_obj_t *p,const char *s,bool spin) { (void)s;if(p==found_list)spinner_visible=spin; }\n'
QUEUE_PREFIX='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n\ntypedef void lv_indev_t;\ntypedef int queue_mode_t;\n#define LV_INDEV_TYPE_POINTER 1\n#define LV_INDEV_STATE_PRESSED 1\n#define QUEUE_POOL 2\ntypedef struct { int index; } row_t;\nstatic row_t queue_rows[QUEUE_POOL]={{0},{1}};\nstatic unsigned queue_seen_revision=4,queue_seen_metadata_revision=3;\nstatic int queue_seen_current=0,queue_seen_mode=2, layouts, binds;\nstatic unsigned metadata=3;\nstatic bool page_up=true;\nstatic char outside_snapshot[40]="Station connecting", painted_note[40]="Station connecting";\nstatic bool queue_page_up(void) { return page_up; }\nstatic unsigned playlist_revision(void) { return 4; }\nstatic int playlist_current_index(void) { return 0; }\nstatic queue_mode_t queue_mode_now(void) { return 2; }\nstatic unsigned device_state_metadata_revision(void) { return metadata; }\nstatic void queue_layout(void) { layouts++; snprintf(painted_note,sizeof(painted_note),"%s",outside_snapshot); }\nstatic lv_indev_t *lv_indev_get_next(lv_indev_t *p) { (void)p; return NULL; }\nstatic int lv_indev_get_type(lv_indev_t *p) { (void)p;return 0; }\nstatic int lv_indev_get_state(lv_indev_t *p) { (void)p;return 0; }\nstatic void queue_row_bind(row_t *p,int i) { (void)p;(void)i;binds++; }\n'

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[1])
parser.add_argument('--reviewed-overlay',type=Path)
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
TREE=args.source_root.resolve()
def source_file(path):
    overlay=args.reviewed_overlay/path if args.reviewed_overlay else None
    return overlay if overlay and overlay.is_file() else TREE/path



def main():
    sanitize=args.sanitize
    home=(source_file('src/gui/shell/main_menu.c')).read_text()
    top=(source_file('src/gui/shell/topbar.c')).read_text()
    track=(source_file('src/gui/nowplaying/trackmenu.c')).read_text()
    bttext=(source_file('src/gui/bluetooth/btsettings.c')).read_text()
    backend=source_file('src/system/bluetooth/bluetooth.c').read_text()
    bt_c=BT_PREFIX+function(backend,'do_scan')+'\n'+function(bttext,'rebuild_lists')+'\n'+function(bttext,'poll_cb')+r"""
int main(void) {
    assert(!drawn_scanning); // Fresh page before first 500-ms poll.
    g_scanning=true; rebuild_lists(); assert(spinner_visible);
    do_scan(); assert(!g_scanning);
    int before=list_rebuilds; poll_cb(NULL);
    assert(list_rebuilds==before+1 && !spinner_visible);
    g_scanning=true; rebuild_lists(); assert(spinner_visible);
    g_scanning=false; poll_cb(NULL); assert(!spinner_visible);
    before=list_rebuilds;
    for(int i=0;i<1000;i++) poll_cb(NULL); assert(list_rebuilds==before);
    puts("PASS Bluetooth: actual optimistic rebuild, fresh-false cache, refusal before first poll, stop, no unchanged rebuild");
}
"""
    # This is the actual layout's cache commit, not an independent policy.
    layout=function(track,'queue_layout')
    commit='snprintf(queue_seen_outside, sizeof(queue_seen_outside), "%s", outside);'
    if layout.count(commit)!=1:
        raise RuntimeError('Actual queue layout must commit its displayed outside title exactly once')
    home_c=COMMON+r'''
typedef void lv_timer_t;
typedef int lv_obj_t;
typedef struct { int state; } wifi_status_t;
#define WIFI_STATE_CONNECTED 1
#define LV_EVENT_REFRESH 5
static lv_obj_t home_page,more_page,other_page;
static lv_obj_t *main_menu_screen=&home_page,*morepage_screen=&more_page,*active=&other_page;
static int drawn_network_state=-1,wifi_state,bt_connected,snapshots,events,top_polls;
static char displayed[96];
static lv_obj_t *lv_screen_active(void) { return active; }
static void wifi_get_status(wifi_status_t *w) { snapshots++;w->state=wifi_state; }
static bool bluetooth_connected_device(void *x) { (void)x;return bt_connected; }
static const char *tr(const char *x) { return x; }
static void network_description(char *text,size_t size);
static void lv_obj_send_event(lv_obj_t *o,int e,void *p) { (void)o;(void)e;(void)p;events++; network_description(displayed,sizeof(displayed)); }
static void topbar_refresh_radios(void) { top_polls++; }
'''
    for name in ('network_state','network_description','main_menu_poll_network'):
        home_c+=function(home,name)+'\n'
    home_c+=function(top,'radio_timer_cb')+r'''
int main(void) {
    for(int i=0;i<100;i++) radio_timer_cb(NULL);
    assert(!snapshots && !events && top_polls==100);
    active=main_menu_screen;
    network_description(displayed,sizeof(displayed));
    assert(!strcmp(displayed,"home_no_connections"));
    radio_timer_cb(NULL); assert(!events);
    wifi_state=1; radio_timer_cb(NULL);
    assert(events==1 && !strcmp(displayed,"home_wifi_connected"));
    for(int i=0;i<1000;i++) radio_timer_cb(NULL);
    assert(events==1);
    bt_connected=1; radio_timer_cb(NULL); assert(events==1); // Wi-Fi priority.
    wifi_state=0; radio_timer_cb(NULL);
    assert(events==2 && !strcmp(displayed,"home_bt_connected"));
    bt_connected=0; radio_timer_cb(NULL);
    assert(events==3 && !strcmp(displayed,"home_no_connections"));
    active=&other_page; int old=snapshots;
    wifi_state=1; for(int i=0;i<100;i++) radio_timer_cb(NULL);
    assert(snapshots==old && events==3);
    active=morepage_screen; radio_timer_cb(NULL);
    assert(events==4 && !strcmp(displayed,"home_wifi_connected"));
    active=main_menu_screen; network_description(displayed,sizeof(displayed));
    radio_timer_cb(NULL); assert(events==4);
    puts("PASS Home: async on/off, BT/Wi-Fi priority, More/reentry, hidden no getters/events, 1000 unchanged polls");
}
'''
    queue_c=QUEUE_PREFIX
    queue_c=queue_c.replace('static unsigned metadata=3;', 'static unsigned metadata=3;\nstatic char queue_seen_outside[256]="Station connecting";\nstatic bool pressed;\nstatic int scroll_position=123, outside_reads;')
    queue_c=queue_c.replace('static void queue_layout(void) { layouts++; snprintf(painted_note,sizeof(painted_note),"%s",outside_snapshot); }',
        'static void queue_layout(void) { char outside[256]; snprintf(outside,sizeof(outside),"%s",outside_snapshot); '+commit+' layouts++; snprintf(painted_note,sizeof(painted_note),"%s",outside_snapshot); }')
    queue_c=queue_c.replace('return NULL; }','return !p && pressed ? (lv_indev_t *)1 : NULL; }',1)
    queue_c=queue_c.replace('static int lv_indev_get_type(lv_indev_t *p) { (void)p;return 0; }','static int lv_indev_get_type(lv_indev_t *p) { (void)p;return LV_INDEV_TYPE_POINTER; }')
    queue_c=queue_c.replace('static int lv_indev_get_state(lv_indev_t *p) { (void)p;return 0; }','static int lv_indev_get_state(lv_indev_t *p) { (void)p;return pressed ? LV_INDEV_STATE_PRESSED : 0; }')
    queue_c=queue_c.replace('static queue_mode_t queue_mode_now(void) { return 2; }','')
    queue_c+='''
#define QUEUE_MODE_PARKED 1
#define QUEUE_MODE_BESIDE 2
#define QUEUE_MODE_IDLE 3
#define QUEUE_MODE_QUEUE 4
static int fake_count=2;
static bool fake_book, fake_dlna, fake_mini=true;
static bool audiobook_is_playing(void) { return fake_book; }
static bool dlna_owns_playback(void) { return fake_dlna; }
static int playlist_count(void) { return fake_count; }
static bool sx_mini_has_track(void) { return fake_mini; }
static bool player_outside_title(char *o,size_t n) { outside_reads++;snprintf(o,n,"%s",outside_snapshot);return o[0]!=0; }
'''
    # Actual helper and existing edit-route wrapper are both exercised.
    import re
    helper=re.search(r'static queue_mode_t queue_mode_for_outside\(bool has_outside\) \{[\s\S]*?\n\}',track)
    wrapper=re.search(r'static queue_mode_t queue_mode_now\(void\) \{[\s\S]*?\n\}',track)
    if not helper or not wrapper:
        raise RuntimeError('Missing shared outside snapshot mode helper/wrapper')
    queue_c+=helper.group(0)+'\n'+wrapper.group(0)+'\n'
    queue_c+=function(track,'trackmenu_queue_poll')+r'''
int main(void) {
    for(int i=0;i<1000;i++) trackmenu_queue_poll(); assert(!layouts && !binds && outside_reads==1000);
    snprintf(outside_snapshot,sizeof(outside_snapshot),"Station actual name");
    pressed=true; metadata++;
    for(int i=0;i<20;i++) trackmenu_queue_poll();
    assert(!layouts && !binds && !strcmp(painted_note,"Station connecting"));
    pressed=false; trackmenu_queue_poll();
    assert(layouts==1 && !strcmp(painted_note,"Station actual name") && scroll_position==123);
    // The real layout also commits metadata revision while rebuilding rows.
    queue_seen_metadata_revision=metadata;
    for(int i=0;i<1000;i++) trackmenu_queue_poll(); assert(layouts==1 && !binds);
    snprintf(outside_snapshot,sizeof(outside_snapshot),"Different same-mode source");
    trackmenu_queue_poll(); assert(layouts==2 && !strcmp(painted_note,outside_snapshot));
    page_up=false; snprintf(outside_snapshot,sizeof(outside_snapshot),"Hidden title");
    int previous_reads=outside_reads;
    for(int i=0;i<20;i++) trackmenu_queue_poll(); assert(layouts==2 && outside_reads==previous_reads);
    page_up=true; trackmenu_queue_poll(); assert(layouts==3 && !strcmp(painted_note,outside_snapshot));
    metadata++; trackmenu_queue_poll(); assert(binds==QUEUE_POOL && layouts==3);
    assert(scroll_position==123);
    assert(queue_mode_now()==QUEUE_MODE_BESIDE);
    fake_book=true; assert(queue_mode_now()==QUEUE_MODE_PARKED); fake_book=false;
    fake_dlna=true; assert(queue_mode_now()==QUEUE_MODE_PARKED); fake_dlna=false;
    outside_snapshot[0]=0; assert(queue_mode_now()==QUEUE_MODE_QUEUE);
    fake_count=0; fake_mini=false; assert(queue_mode_now()==QUEUE_MODE_IDLE);
    puts("PASS queue: title-only same-mode update, finger deferral/retry, hidden/reentry, no unchanged rebuild, existing metadata rows");
}
'''
    results={}
    cases=[('bluetooth',bt_c,False),('home',home_c,False),('queue',queue_c,False),
      ('negative-bluetooth-missing-scan-invalidation',bt_c.replace('scanning != drawn_scanning','false && scanning != drawn_scanning'),True),
      ('negative-bluetooth-poll-only-cache',bt_c.replace('drawn_scanning = scanning;','').replace('drawn_enabled = on;','drawn_enabled = on; drawn_scanning=scanning;'),True),
      ('negative-home-missing-radio-hook',home_c.replace('\n\tmain_menu_poll_network();','\n\t(void)main_menu_poll_network;'),True),
      ('negative-queue-missing-title-invalidation',queue_c.replace('if (strcmp(outside, queue_seen_outside) != 0)', 'if (false && strcmp(outside, queue_seen_outside) != 0)'),True),
      ('negative-queue-no-pointer-deferral',queue_c.replace('lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED','false && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED',1),True)]
    with tempfile.TemporaryDirectory(prefix='sonix-ui-fixed-') as tmp:
        for name,source,negative in cases:
            c=Path(tmp)/(name+'.c'); exe=Path(tmp)/name; c.write_text(source)
            cmd=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-format-zero-length','-Wno-parentheses','-pthread']
            if sanitize:
                cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
            subprocess.run(cmd+[str(c),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            if (not negative and run.returncode!=0) or (negative and (run.returncode>=0 or 'Assertion' not in run.stderr)):
                raise RuntimeError(name+': unexpected runtime result '+str(run.returncode)+' '+run.stderr)
            results[name]={'passed':True,'negative_runtime_assertion':negative,'returncode':run.returncode,'stdout':run.stdout,'stderr':run.stderr}
    result={'sanitized':sanitize,'results':results,'production_mutated':False,'adb_used':False,
            'review_copy_sha256':{p:hashlib.sha256(source_file(p).read_bytes()).hexdigest() for p in ['src/gui/bluetooth/btsettings.c','src/gui/shell/main_menu.c','src/gui/shell/topbar.c','src/gui/nowplaying/trackmenu.c']}}
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
