"""Actual menu/dialog context: an old action cannot adopt a newer listing/card."""
from pathlib import Path
import os,re,resource,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'src/gui/library/filespage.c').read_text()
def function(name):
 m=re.search(r'^static (?:bool|int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 end=m.end();depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]+'\n'
body=''.join(function(n) for n in ('capture_action','action_current','action_child','name_is_usable'))
prefix=r'''
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "src/system/device/fileops.h"
static bool op_running;
static atomic_bool op_closing;
static storage_card_token_t list_card,action_card;
static unsigned list_database_epoch,action_database_epoch;
static char path[FILEOP_ROOT_MAX],root_path[FILEOP_ROOT_MAX],action_path[FILEOP_ROOT_MAX],action_root[FILEOP_ROOT_MAX];
static uint64_t owner;
static void gui_notify_popup(const char *s){assert(s);}
int storage_card_try_check(const storage_card_token_t *t){if(t->epoch&&t->epoch==owner)return 0;errno=ESTALE;return -1;}
'''
main=r'''
int main(void){
 owner=4;list_card.epoch=4;list_database_epoch=11;
 snprintf(path,sizeof(path),"/old/album");snprintf(root_path,sizeof(root_path),"/old");
 assert(capture_action()&&action_current()&&action_card.epoch==4&&action_database_epoch==11);
 snprintf(path,sizeof(path),"/old/other-folder");
 char full[FILEOP_PATH_MAX];assert(action_child("song.flac",full,sizeof(full))&&!strcmp(full,"/old/album/song.flac"));
 assert(!action_child("song.flac",full,4));
 owner=5;assert(!action_current());assert(!capture_action());
 list_card.epoch=5;list_database_epoch=12;
 assert(!action_current()); // A newer listing must not revive the old dialog.
 assert(capture_action()&&action_current()&&action_database_epoch==12);
 op_running=true;assert(!capture_action()&&!action_current());op_running=false;
 atomic_store(&op_closing,true);assert(!capture_action()&&!action_current());
 assert(!name_is_usable("..")&&!name_is_usable(".")&&!name_is_usable("a/b")&&name_is_usable("quoted ' name"));
 puts("PASS actual file menu/picker/name context keeps the originating card and folder");
}
'''
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='sonix-files-context-') as d:
 folder=Path(d)
 negative=body.replace('static bool action_current(void) {','static bool action_current(void) { action_card=list_card;',1)
 assert negative!=body
 for name,code in [('production',body),('retokenize-negative',negative)]:
  c=folder/(name+'.c');exe=folder/name;c.write_text(prefix+code+main)
  flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(root),'-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
  subprocess.run(flags+[str(c),'-o',str(exe)],check=True)
  p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
  if name=='production':assert p.returncode==0,p.stderr;print(p.stdout.strip())
  else:assert p.returncode<0 and 'Assertion' in p.stderr,p.stderr;print('PASS old-context retokenization negative control')
