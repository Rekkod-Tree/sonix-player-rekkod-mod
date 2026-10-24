"""Verify the production queue/chapter openers preserve the caller's Back path."""
from pathlib import Path
import argparse,hashlib,json,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--results',type=Path,required=True);a=p.parse_args();a.results.mkdir(exist_ok=False)
root=Path(__file__).resolve().parents[1]
files=[root/'src/gui/nowplaying/trackmenu.c',root/'src/gui/nowplaying/chapters.c',Path(__file__).resolve()]
pins={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
def function(path,name):
 text=path.read_text();start=text.index('void '+name+'(void) {');end=text.index('\n}',start)+2
 return text[start:end]
source='''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef void lv_obj_t;
static lv_obj_t *queue_screen=(lv_obj_t *)1, *chapters_screen=(lv_obj_t *)2;
static lv_obj_t *shown, *return_to;
static bool player, book;
static int switches;
static bool player_sheet_is_open(void) {return player;}
static void player_sheet_close(bool animate) {(void)animate;player=false;}
static bool audiobook_is_playing(void) {return book;}
static void switch_screen(lv_obj_t *s) {shown=s;switches++;}
static void switcher_set_player_return(lv_obj_t *s) {return_to=s;}
'''
source+=function(files[1],'chapters_open')+'\n'+function(files[0],'trackmenu_open_queue')
source+='''
int main(void) {
 for (int from=0;from<2;from++) for(int is_book=0;is_book<2;is_book++) {
  player=from;book=is_book;shown=NULL;return_to=(lv_obj_t *)99;switches=0;
  trackmenu_open_queue();
  assert(shown==(is_book?chapters_screen:queue_screen));
  assert(return_to==(from?shown:NULL));
  assert(!player && switches==1);
 }
 return 0;
}
'''
fixture=a.results/'queue-return.c';fixture.write_text(source);binary=a.results/'queue-return'
cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(fixture),'-o',str(binary)]
r=subprocess.run(cmd,text=True,capture_output=True,timeout=60);(a.results/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(binary)],text=True,capture_output=True,timeout=30);(a.results/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
assert all(hashlib.sha256(Path(n).read_bytes()).hexdigest()==h for n,h in pins.items())
(a.results/'result.json').write_text(json.dumps(dict(passed=True,sources=pins,cases=4),indent=2)+'\n')
print('PASS actual queue and chapter openers: player-origin returns to player, section-origin returns to section.')
