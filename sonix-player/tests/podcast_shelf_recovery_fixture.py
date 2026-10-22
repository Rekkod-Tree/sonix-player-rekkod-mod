# Focused actual shelf-scan functions with real filesystem and pair readiness.
# Display metadata/history are fixtures; no LVGL/device/network.
from pathlib import Path
import re, sys
s=Path('src/gui/streaming/podcastcard.c').read_text()
def function(name):
 m=re.search(r'(?:static )?[^;\n]+\b'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 i=m.end();depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[m.start():i]+'\n'
text=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "src/system/streaming/podcastpair.h"
typedef struct {char name[256],folder[600],first[600];int episodes,unheard;bool fresh,downloaded;long long feed_id;} podcastcard_show_t;
static bool last_scan_pending,create_during_count;
static char root[512];
static unsigned counted;
static void podcastdl_root(char *out,size_t n){snprintf(out,n,"%s",root);}
static void heard_read(const char *p){(void)p;}
static void heard_free(void){}
static bool playlist_is_playable_file(const char *p){size_t n=strlen(p);return n>4&&!strcasecmp(p+n-4,".mp3");}
static bool podcastcache_tag(const char *p,const char*k,char*v,size_t n){(void)p;(void)k;(void)v;(void)n;return false;}
static void touch(const char *p){FILE *f=fopen(p,"wb");assert(f);assert(fwrite("x",1,1,f)==1);assert(!fclose(f));}
static void count_file(const char *p,podcastcard_show_t *show){show->episodes++;counted++;if(create_during_count){create_during_count=false;char buf[900];snprintf(buf,sizeof(buf),"%s/.sonix-pair.txn",root);touch(buf);}(void)p;}
'''
for name in ['is_episode_name','entry_is','is_sidecar_name','count_folder','show_cmp','podcastcard_scan_pending','podcastcard_shows']:text+=function(name)
text+=r'''
static void scan(bool pending,int count){podcastcard_show_t shows[8];counted=0;assert(podcastcard_shows(shows,8)==count);if(podcastcard_scan_pending()!=pending)fprintf(stderr,"pending wanted=%d got=%d counted=%u\n",pending,podcastcard_scan_pending(),counted);assert(podcastcard_scan_pending()==pending);}
int main(void){
 char tmp[]="/tmp/sonix-shelf-pending-XXXXXX";assert(mkdtemp(tmp));snprintf(root,sizeof(root),"%s",tmp);char path[900],marker[900];
 snprintf(path,sizeof(path),"%s/one.mp3",root);touch(path);scan(false,1);assert(counted==1);
 snprintf(marker,sizeof(marker),"%s/.sonix-pair.txn",root);touch(marker);scan(true,0);assert(!counted);assert(!unlink(marker));scan(false,1);
 snprintf(path,sizeof(path),"%s/Empty",root);assert(!mkdir(path,0700));snprintf(marker,sizeof(marker),"%s/Empty/.sonix-pair.txn.next",root);touch(marker);scan(true,1);assert(!unlink(marker));scan(false,1);
 snprintf(path,sizeof(path),"%s/two.mp3",root);touch(path);create_during_count=true;scan(true,1);assert(counted==1);
 snprintf(marker,sizeof(marker),"%s/.sonix-pair.txn",root);assert(!unlink(marker));scan(false,1);assert(counted==2);
 assert(podcastcard_shows(NULL,0)==0&&!podcastcard_scan_pending());
 puts("PASS actual shelf scan: normal, malformed marker, orphan empty folder, mid-scan publication, GUI flag resets");
 return 0;
}
'''
Path(sys.argv[1]).write_text(text)
