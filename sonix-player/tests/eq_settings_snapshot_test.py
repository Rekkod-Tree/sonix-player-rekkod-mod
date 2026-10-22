"""Actual EQ publication/barrier checks; optional explicit baseline PCM comparison."""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent)
p.add_argument('--reviewed-overlay',type=Path)
p.add_argument('--baseline-eq',type=Path,help='Optional explicit old eq.c for assertion witness and byte-identical paired PCM; its hash is recorded')
p.add_argument('--results',type=Path)
p.add_argument('--sanitize',action='store_true');p.add_argument('--negative',action='store_true');args=p.parse_args()
F=Path(__file__).resolve().parent;ROOT=args.source_root.resolve();O=args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
path=O/'src/system/audio/eq.c';source=path.read_text(encoding='utf-8')
baseline=args.baseline_eq.resolve() if args.baseline_eq else None
baseline_raw=baseline.read_bytes() if baseline else None
paths=[path,O/'src/system/audio/eq.h',F/'eq_settings_snapshot_test.c',F/'eq_pcm_probe.c']
if baseline:paths.append(baseline)
pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}

def frange(s,name):
 m=list(re.finditer(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+'+name+r'\([^;]*?\)\s*\{',s,re.M))[-1];end,depth=m.end(),1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return m.start(),end
def mutate(s,name,a,b):
 x,y=frange(s,name);text=s[x:y];assert text.count(a)==1,(name,a);return s[:x]+text.replace(a,b)+s[y:]
def instrument(s):
 seams=[('atomic_store_explicit(&settings_version, published_settings.version, memory_order_release);','\n\tfixture_published();'),('unsigned wanted = atomic_load_explicit(&settings_version, memory_order_relaxed);','\n\tfixture_hint();'),('slot_update(slot, type, freq, gain, (double)b->q_cent / 100.0, rate, rate_changed);','\n\t\tfixture_rebuilt_slot(slot);'),('mseb_weight_ready = true;','\n\tfixture_weights();')]
 for old,tail in seams:assert s.count(old)==1,old;s=s.replace(old,old+tail,1)
 return s
variants=[('actual',source,False)]
if args.negative:
 variants += [('late-version-latch',mutate(source,'filters_rebuild','built_version = settings->version;','built_version = atomic_load_explicit(&settings_version,memory_order_relaxed);'),True),
 ('blocking-audio-lock',mutate(source,'settings_accept','pthread_mutex_trylock(&settings_lock)','pthread_mutex_lock(&settings_lock)'),True),
 ('copy-payload-on-busy',mutate(source,'settings_accept','wanted != audio_settings.version && pthread_mutex_trylock(&settings_lock) == 0','wanted != audio_settings.version').replace('audio_settings = published_settings;\n\t\tpthread_mutex_unlock(&settings_lock);','audio_settings = published_settings;',1),True),
 ('alloff-before-adoption',mutate(source,'process_prepare','\tsettings_accept();\n\tif (!audio_settings.eq_on && !audio_settings.mseb_on && !audio_settings.peq_on)\n\t\treturn false;', '\tif (!audio_settings.eq_on && !audio_settings.mseb_on && !audio_settings.peq_on)\n\t\treturn false;\n\tsettings_accept();'),True),
 ('skip-rate-rebuild',mutate(source,'process_prepare','built_rate != sample_rate','false'),True),
 ('live-mseb-normalization',mutate(source,'mseb_bank_build','settings->mseb_value[MSEB_DEFS[i].slider]','mseb_value[MSEB_DEFS[i].slider]'),True),
 ('copy-version-from-hint',mutate(source,'settings_accept','audio_settings = published_settings;','audio_settings = published_settings; audio_settings.version = wanted;'),True),
 ('missing-preamp-publication',mutate(source,'peq_set_preamp','settings_publish();','(void)0;'),True),
 ('late-weight-initialization',mutate(source,'eq_init','\tmseb_weights_build();',''),True)]
resource.setrlimit(resource.RLIMIT_CORE,(0,0));records=[];pcm=[]
with tempfile.TemporaryDirectory(prefix='eq62-') as temporary:
 t=Path(temporary);out=args.results.parent/(args.results.stem+'-logs') if args.results else t/'logs';out.mkdir(parents=True,exist_ok=True);flags=['cc','-std=gnu11','-O3','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-I'+str(t),'-I'+str(O),'-I'+str(ROOT),'-I'+str(O/'src/system/audio'),'-I'+str(ROOT/'src/system/audio')];links=['-Wl,--gc-sections','-pthread','-lm']
 if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie'];links+=['-fsanitize=address,undefined','-no-pie']
 def build(name,text,fixture,extra=()):
  (t/'actual_eq.c').write_text(text,encoding='utf-8',newline='\n');binary=t/name
  r=subprocess.run([*flags,*extra,str(F/fixture),*links,'-o',str(binary)],capture_output=True,text=True,encoding='utf-8');(out/(name+'-build.log')).write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n');assert r.returncode==0,(name,r.stderr);return binary
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
 for name,text,negative in variants:
  assert not negative or text!=source
  binary=build(name,instrument(text),'eq_settings_snapshot_test.c');r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=25,env=env)
  (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n')
  if negative:assert r.returncode<0 and 'Assertion' in r.stderr,(name,r.returncode,r.stdout,r.stderr)
  else:assert r.returncode==0,(name,r.returncode,r.stdout,r.stderr)
  records.append({'name':name,'negative':negative,'returncode':r.returncode,'passed':True});print('PASS',name,flush=True)
 if baseline_raw is not None:
  # Demonstrate the old defect by rejecting the same coherence invariant on
  # actual baseline code, not a behavioral imitation of that implementation.
  old=baseline_raw.decode('utf-8').replace('\r\n','\n');seam='slot_update(slot, type, freq, gain, (double)b->q_cent / 100.0, rate, rate_changed);';assert old.count(seam)==1
  binary=build('broken-baseline',old.replace(seam,seam+'\n\t\tfixture_slot(slot);',1),'eq_pcm_probe.c',('-DBASELINE_CONTROL=1',));r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=20,env=env)
  assert r.returncode<0 and 'Assertion' in r.stderr;(out/'broken-baseline.log').write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n');records.append({'name':'actual-baseline-coherence-refusal','passed':True,'expected_abort':True})
  binaries={name:build('pcm-'+name,text,'eq_pcm_probe.c') for name,text in (('baseline',old),('candidate',source))}
  cases=((0,48000,2,16),(1,44100,1,16),(2,32000,2,16),(3,48000,2,16),(4,96000,2,32),(4,192000,1,32),(4,384000,2,32),(2,44100,1,32),(1,96000,2,16),(3,48000,2,32));pcm=[]
  for case in cases:
   outputs={}
   for name,binary in binaries.items():
    r=subprocess.run([str(binary),*map(str,case)],capture_output=True,timeout=20,env=env);assert r.returncode==0,(name,case,r.stderr.decode('utf-8'));outputs[name]=r.stdout
   assert outputs['baseline']==outputs['candidate'],case
   assert len(outputs['candidate'])==4*512*case[2]*(case[3]//8)
   pcm.append({'case':case,'bytes':len(outputs['candidate']),'sha256':hashlib.sha256(outputs['candidate']).hexdigest(),'bit_identical':True})
  print('PASS paired actual baseline/candidate PCM: 10 configurations S16/S32 mono/stereo 32–384 kHz',flush=True)
assert pins=={str(x):hashlib.sha256(Path(x).read_bytes()).hexdigest() for x in pins}
result={'passed':True,'sanitized':args.sanitize,'pins':pins,'protocol':records,'pcm':pcm,'paired_baseline_supplied':baseline is not None,'production_changed':False,'native_executed':False}
if args.results:
 args.results.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
else:print(json.dumps(result,indent=2))
