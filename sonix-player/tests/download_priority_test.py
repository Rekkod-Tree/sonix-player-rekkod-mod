#!/usr/bin/env python3
"""Exercise real registry and cache-yield bodies with concurrent fake transfers.

python3 tests/download_priority_test.py --sanitize --output /tmp/priority-test
Use --source-root to run the same cases against a pre-fix checkout.
Only service metadata, network close and waiting are stubbed; no card/network I/O.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cc',default='cc')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
root=a.source_root or Path(__file__).resolve().parents[1]
folder=root/'src/system/streaming'
inputs={}
def load(name):
    f=folder/name
    inputs[name]=hashlib.sha256(f.read_bytes()).hexdigest()
    return f.read_text()
def function(text,name):
    m=re.search(r'^(?:static )?(?:bool|int|void|const stream_provider_t \*)\s*'+re.escape(name)+r'\([^\n]*?\)\s*\{',text,re.M)
    assert m,name
    start=m.start();pos=m.end();depth=1
    while depth:
        assert pos<len(text),name
        depth += (text[pos]=='{')-(text[pos]=='}')
        pos+=1
    return text[start:pos]
provider=load('provider.c');header=load('provider.h')
has_api='stream_provider_playback_downloading' in provider
registry='static stream_provider_t *providers[STREAM_PROVIDER_MAX];\nstatic int provider_count;\n'
for name in ['stream_provider_register','stream_provider_count','stream_provider_at',
             'stream_provider_any_downloading','stream_provider_any_network_wanted']:
    registry+=function(provider,name)+'\n'
if has_api:
    registry+=function(provider,'stream_provider_playback_downloading')+'\n'
clients=[];bindings=[]
for service,prefix in [('yandex','ym'),('zvuk','zv')]:
    sync=load(service+'sync.c');cache=load(service+'cache.c')
    clients.append(function(sync,'provider_downloading').replace('provider_downloading',prefix+'_provider_downloading'))
    for name in ['others_downloading','give_way']:
        clients.append(function(cache,name).replace('others_downloading',prefix+'_others_downloading').replace('give_way',prefix+'_give_way'))
    if has_api:
        match=re.search(r'\.playback_downloading\s*=\s*(\w+)\s*,',sync)
        assert match and match[1]==service+'cache_busy','Wrong playback hook wiring'
        bindings.append(prefix+'.playback_downloading='+match[1]+';')
fixture=Path(__file__).with_suffix('.c').read_text()
for marker,text in {'HAS_PLAYBACK_API':'#define HAS_PLAYBACK_API '+str(int(has_api)),
                    'PRODUCTION_REGISTRY':registry,'PRODUCTION_CLIENTS':'\n'.join(clients),
                    'PLAYBACK_BINDINGS':'\n'.join(bindings)}.items():
    key='/* '+marker+' */';assert fixture.count(key)==1
    fixture=fixture.replace(key,text)
a.output.mkdir(parents=True,exist_ok=False)
f=a.output/'fixture.c';f.write_text(fixture)
command=[a.cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root),str(f),'-pthread','-o',str(a.output/'test')]
if a.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
with (a.output/'compile.log').open('w') as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
r=subprocess.run([str(a.output/'test')],capture_output=True,text=True,timeout=20)
(a.output/'run.log').write_text(r.stdout+r.stderr)
(a.output/'result.json').write_text(json.dumps(dict(passed=r.returncode==0,returncode=r.returncode,
 source_sha256=inputs,fixture_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),sanitized=a.sanitize),indent=2)+'\n')
print(r.stdout,end='');print(r.stderr,end='')
raise SystemExit(r.returncode)
