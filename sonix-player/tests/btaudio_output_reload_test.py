"""Exercise the production route reload call site without a GUI or sound device."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parent.parent
source=(root/'src/gui/bluetooth/btaudio.c').read_text()
name='restart_playback_if_running'
m=re.search(r'^static void '+name+r'\(void\) \{',source,re.M);assert m
end=m.end();depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
function=source[m.start():end]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/system/audio/audio.h"
static audio_status_t status;
static int called, refreshed;
static char path[512];
void audio_get_current_file(char *out, size_t size) {snprintf(out,size,"%s",path);}
audio_status_t audio_get_status(void) {return status;}
void audio_get_progress(double *pos,double *total) {*pos=92.5;*total=180;}
int audio_play_at(const char *file,double pos) {assert(status==AUDIO_STATUS_PLAYING);assert(!strcmp(file,path)&&pos==92.5);called++;return 0;}
int audio_play_paused(const char *file,double pos) {assert(status==AUDIO_STATUS_PAUSED);assert(!strcmp(file,path)&&pos==92.5);called++;return 0;}
int audio_play(const char *file) {(void)file;assert(!"non-atomic zero-position playback");return 0;}
void audio_seek(double position) {(void)position;assert(!"separate seek must not follow a replay");}
void audio_pause(void) {assert(!"separate pause must not follow a replay");}
void player_refresh_now_playing(void) {refreshed++;}
@FUNCTION@
int main(void) {
 strcpy(path,"/card/book/chapter.m4a");
 status=AUDIO_STATUS_PLAYING;restart_playback_if_running();assert(called==1&&refreshed==1);
 status=AUDIO_STATUS_PAUSED;restart_playback_if_running();assert(called==2&&refreshed==2);
 status=AUDIO_STATUS_STOPPED;restart_playback_if_running();assert(called==2&&refreshed==2);
 path[0]=0;status=AUDIO_STATUS_PLAYING;restart_playback_if_running();assert(called==2&&refreshed==2);
 puts("PASS: production route reload selects atomic play-at/paused-at; stopped/empty sources remain untouched");
}
'''
with tempfile.TemporaryDirectory(prefix='sonix-route-reload-') as folder:
 p=Path(folder)/'test.c';p.write_text(fixture.replace('@FUNCTION@',function));exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
