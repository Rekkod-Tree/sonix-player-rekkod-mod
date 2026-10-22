"""Actual radio/state/poll/mini functions with inert audio and label operations.
No network, device, source writes or production builds. UTF-8/LF throughout.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile

def closing(text, opening):
    depth=1;i=opening+1;state='code'
    while depth:
        a,b=text[i],text[i:i+2]
        if state=='line':
            if a=='\n':state='code'
        elif state=='block':
            if b=='*/':state='code';i+=1
        elif state in ('"',"'"):
            if a=='\\':i+=1
            elif a==state:state='code'
        elif b=='//':state='line';i+=1
        elif b=='/*':state='block';i+=1
        elif a in ('"',"'"):state=a
        elif a=='{':depth+=1
        elif a=='}':depth-=1
        i+=1
    return i

def function(text,name):
    m=re.search(r'^(?:static )?(?:void|bool|unsigned|int) '+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M)
    assert m,name
    return text[m.start():closing(text,text.index('{',m.start()))]+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--reviewed-overlay',type=Path)
    p.add_argument('--results',type=Path,required=True)
    p.add_argument('--sanitize',action='store_true')
    p.add_argument('--negative',action='store_true')
    p.add_argument('--baseline-player',type=Path)
    args=p.parse_args();root=args.source_root.resolve();overlay=args.reviewed_overlay
    if (root/'sonix-player').is_dir():root/='sonix-player'
    if overlay and (overlay/'sonix-player').is_dir():overlay/='sonix-player'
    args.results.mkdir(parents=True,exist_ok=True)
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    pins={}
    def read(name):
        path=overlay/name if overlay and (overlay/name).is_file() else root/name
        pins[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
        return path.read_text(encoding='utf-8')
    radio=read('src/system/streaming/radio.c');state=read('src/system/playback/device_state.c')
    mini=read('src/gui/shell/sx_mini.c');player=read('src/gui/nowplaying/player.c')
    fixture=Path(__file__).with_suffix('.c');pins[str(fixture)]=hashlib.sha256(fixture.read_bytes()).hexdigest()
    # The unchanged production poll block is compiled verbatim, not re-modelled.
    update=function(player,'update_progress')
    start=update.index('\t{\n\t\tstatic bool was_live;')
    block=update[start:closing(update,update.index('{',start))]
    snapshot_start=update.index('\tdevice_state_t state;')
    serial='\tunsigned serial = radio_now_serial();\n'
    earlier=update.find(serial)
    if earlier>=0 and earlier<snapshot_start:
        snapshot_start=earlier
    snapshot_end=update.index('\tdevice_state_get(&state);')+len('\tdevice_state_get(&state);')
    snapshot=update[snapshot_start:snapshot_end]+'\n'
    radio_functions=('now_touch','radio_push_pcm','radio_push_pcm32','radio_play','radio_stop','radio_resume','radio_clear','radio_is_active','radio_get_now','radio_now_serial')
    mini_functions=('remember_text','compact_text','sx_mini_set_text','sx_mini_set_track','sx_mini_set_playing')
    out_type=re.search(r'typedef struct \{\n\tbool open;.*?\} radio_out_t;',radio,re.S).group()
    base='\n'.join([out_type,*[function(radio,n) for n in radio_functions],function(state,'device_state_get'),*[function(mini,n) for n in mini_functions]])
    live=function(player,'refresh_live_texts')
    variants=[('actual',base+live,block,snapshot,False)]
    if args.baseline_player:
        old=args.baseline_player.read_text(encoding='utf-8')
        old_update=function(old,'update_progress')
        old_start=old_update.index('\t{\n\t\tstatic bool was_live;')
        old_block=old_update[old_start:closing(old_update,old_update.index('{',old_start))]
        old_snapshot_start=old_update.index('\tdevice_state_t state;')
        old_snapshot_end=old_update.index('\tdevice_state_get(&state);')+len('\tdevice_state_get(&state);')
        variants.append(('real-baseline',base+function(old,'refresh_live_texts'),old_block,old_update[old_snapshot_start:old_snapshot_end]+'\n',True))
    if args.negative:
        statement='\tsx_mini_set_text(state->metadata.title, state->metadata.artist);\n'
        assert live.count(statement)==1
        assert snapshot.count(serial)==1 and 'radio_now_serial()' not in block
        misplaced=block.replace('\t\tstatic unsigned last_serial;\n','\t\tstatic unsigned last_serial;\n\t\tunsigned serial = radio_now_serial();\n')
        variants += [('missing-mini-refresh',base+live.replace(statement,''),block,snapshot,True),
                     ('discard-icy-and-error',base+live.replace(statement,'\tsx_mini_set_text(state->metadata.title, NULL);\n'),block,snapshot,True),
                     ('full-track-reset-on-status',base+live.replace(statement,'\tsx_mini_set_track(state->metadata.title, NULL, state->metadata.artist, true);\n'),block,snapshot,True),
                     ('redraw-every-poll',base+live,block.replace('state.live && serial != last_serial','state.live && serial >= last_serial'),snapshot,True),
                     ('connecting-not-cleared',base.replace('now_state.connecting = false;\n\t\tnow_state.playing = true;','now_state.connecting = true;\n\t\tnow_state.playing = true;')+live,block,snapshot,True),
                     ('serial-after-snapshot',base+live,misplaced,snapshot.replace(serial,''),True),
                     ('extra-serial-getter',base+live,block,snapshot.replace(serial,serial+'\t(void)radio_now_serial();\n'),True)]
    records=[]
    with tempfile.TemporaryDirectory(prefix='sonix-radio68-') as temporary:
        tmp=Path(temporary)
        for name,body,poll,snapshot_code,broken in variants:
            (tmp/'actual_functions.inc').write_text(body,encoding='utf-8',newline='\n')
            (tmp/'actual_poll_block.inc').write_text(poll,encoding='utf-8',newline='\n')
            (tmp/'actual_poll_snapshot.inc').write_text(snapshot_code,encoding='utf-8',newline='\n')
            flags=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root),'-I'+str(tmp)]
            if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
            binary=tmp/name
            built=subprocess.run([*flags,str(fixture),'-pthread','-o',str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=60)
            (args.results/(name+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8',newline='\n')
            assert built.returncode==0,(name,built.stderr)
            for mode in (0,1):
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
                ran=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,encoding='utf-8',timeout=10,env=env)
                (args.results/f'{name}-{mode}.log').write_text(ran.stdout+ran.stderr,encoding='utf-8',newline='\n')
                assert (ran.returncode<0 and 'Assertion' in ran.stderr) if broken else ran.returncode==0,(name,mode,ran.returncode,ran.stderr)
                records.append({'case':name,'mode':mode,'passed':not broken,'broken_runtime_assertion_rejected':broken})
                print(('REJECTED' if broken else 'PASS'),name,mode,flush=True)
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest()==sha for path,sha in pins.items())
    result={'passed':True,'scope':'ACTUAL_FUNCTION_TEXT_STATE_NOT_LVGL_GEOMETRY_NOT_NATIVE','sanitize':args.sanitize,'pins':pins,'records':records}
    (args.results/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':main()
