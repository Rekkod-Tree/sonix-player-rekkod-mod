"""Test actual file-bitrate and Now Playing format composers without UI or audio IO."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--results', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent

def function(text, name):
    match = re.search(r'^(?:static )?[^;\n]+\b' + name + r'\([^;]*?\)\s*\{', text, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'

decoder = (repo / 'src/system/decode/decode.c').read_text()
player = (repo / 'src/gui/nowplaying/player.c').read_text()
assert decoder.count('dec->bitrate_kbps = complete_file_bitrate(') == 1
base = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
'''
bitrate = base + r'''
static bool growing;
static unsigned reads;
static bool growfile_is_growing(const char *path) { (void)path; return growing; }
static int counted_stat(const char *path, struct stat *info) {
    reads++;
    if (!strcmp(path,"huge")) { *info=(struct stat){.st_size=INT64_MAX,.st_mode=S_IFREG}; return 0; }
    return stat(path,info);
}
#define stat(...) counted_stat(__VA_ARGS__)
''' + function(decoder, 'complete_file_bitrate') + r'''
int main(void) {
    FILE *f=fopen("fixture.flac","wb"); assert(f);
    assert(ftruncate(fileno(f),1250000)==0); fclose(f);
    assert(complete_file_bitrate("fixture.flac",44100,441000,false)==1000 && reads==1);
    assert(complete_file_bitrate("fixture.flac",44100,441000,true)==0 && reads==1);
    growing=true;
    assert(complete_file_bitrate("fixture.flac",44100,441000,false)==0 && reads==1);
    growing=false;
    assert(complete_file_bitrate("fixture.flac",44100,UINT64_MAX,false)==0 && reads==1);
    assert(complete_file_bitrate("fixture.flac",44100,0,false)==0 && reads==1);
    assert(complete_file_bitrate("fixture.flac",0,441000,false)==0 && reads==1);
    assert(complete_file_bitrate("fixture.flac",44100,22050,false)==0 && reads==1);
    assert(complete_file_bitrate("missing.flac",44100,441000,false)==0);
    assert(complete_file_bitrate("huge",44100,441000,false)==0);
    puts("PASS actual complete-file average, growing-cache exclusion, unknown duration and overflow; one metadata read");
    return 0;
}
'''
compose = base + r'''
#include "src/system/audio/audio.h"
typedef struct { bool live; char current_file[512]; int stream_sample_rate; int stream_channels; } device_state_t;
typedef struct { int bitrate; char codec[16]; } radio_now_t;
static bool lossy, usb, available;
static int bits=24, kbps=1000, dsd;
static audio_output_format_t output;
static char codec[16]="FLAC", fake_link[96];
static void radio_get_now(radio_now_t *v) { memset(v,0,sizeof(*v)); }
int audio_get_dsd_multiple(void) { return dsd; }
void audio_get_stream_codec(char *out, size_t size) { snprintf(out,size,"%s",codec); }
int audio_get_stream_bitrate_kbps(void) { return kbps; }
int audio_get_stream_bits(void) { return bits; }
bool audio_stream_is_lossy(void) { return lossy; }
bool audio_get_output_format(audio_output_format_t *out) { *out=output; return available; }
static bool btline_compose(char *out,size_t size) { snprintf(out,size,"%s",fake_link); return fake_link[0]!=0; }
static bool usbaudio_active(void) { return usb; }
static void sx_chain_short(char *out,size_t size,bool *warn) { if(size)out[0]=0; *warn=false; }
static const char *tr(const char *key) { assert(!strcmp(key,"np_chain_kbps")); return "%s · %d kbps"; }
'''
for name in ('np_khz', 'np_file_short', 'np_output_differs', 'np_output_short', 'np_chain_compose'):
    compose += function(player, name)
compose += r'''
int main(void) {
    device_state_t state={.current_file="/card/track.flac",.stream_sample_rate=96000,.stream_channels=2};
    char text[192]; bool warn;
    available=true; output=(audio_output_format_t){48000,16,2,false};
    np_chain_compose(&state,text,sizeof(text),&warn);
    assert(strstr(text,"FLAC 24/96") && strstr(text,"1000 kbps") && strstr(text,"PCM 16/48") && !warn);
    output=(audio_output_format_t){96000,24,2,false};
    np_chain_compose(&state,text,sizeof(text),&warn);
    assert(strstr(text,"1000 kbps") && !strstr(text,"PCM") && !strstr(text,"\xe2\x86\x92"));
    usb=true; output=(audio_output_format_t){48000,16,2,false};
    np_chain_compose(&state,text,sizeof(text),&warn); assert(strstr(text,"USB 16/48"));
    available=false; usb=false;
    np_chain_compose(&state,text,sizeof(text),&warn); assert(!strstr(text,"PCM"));
    snprintf(fake_link,sizeof(fake_link),"LDAC ABR 909 kbps · 44.1 kHz");
    np_chain_compose(&state,text,sizeof(text),&warn);
    assert(strstr(text,"FLAC 1000") && strstr(text,"LDAC ABR 909 kbps") && strlen(text)<96);
    snprintf(fake_link,sizeof(fake_link),"LDAC ABR · 44.1 kHz");
    np_chain_compose(&state,text,sizeof(text),&warn); assert(!strstr(text,"909") && !strstr(text,"990"));
    kbps=0;
    np_chain_compose(&state,text,sizeof(text),&warn); assert(strstr(text,"FLAC 24/96") && !strstr(text,"0 kbps"));
    fake_link[0]=0; kbps=320; lossy=true; snprintf(codec,sizeof(codec),"MP3");
    state.stream_sample_rate=44100; output=(audio_output_format_t){48000,16,2,false}; available=true;
    np_chain_compose(&state,text,sizeof(text),&warn); assert(strstr(text,"MP3 320") && strstr(text,"PCM 16/48"));
    lossy=false; dsd=64; output=(audio_output_format_t){176400,24,2,true};
    np_chain_compose(&state,text,sizeof(text),&warn); assert(strstr(text,"DSD64") && strstr(text,"DoP 24/176.4"));
    puts("PASS actual player composer: source/BT bitrate, PCM/USB changes, unknown values and DoP");
    return 0;
}
'''
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
records = []
with tempfile.TemporaryDirectory(prefix='sonix-format95-') as temporary:
    temporary = Path(temporary)
    for name, code in [('complete-file-bitrate', bitrate), ('player-format', compose)]:
        source = temporary / (name + '.c')
        binary = temporary / name
        source.write_text(code)
        command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(repo),
                   str(source), '-lm', '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        result = subprocess.run([str(binary)], cwd=temporary, capture_output=True, text=True, timeout=10,
                                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip())
        records.append(dict(name=name, passed=True, evidence=result.stdout.strip()))
if args.results:
    args.results.write_text(json.dumps(dict(passed=True, records=records, source_sha256={
        name: hashlib.sha256((repo / name).read_bytes()).hexdigest()
        for name in ('src/system/decode/decode.c', 'src/gui/nowplaying/player.c')}), indent=2) + '\n')
