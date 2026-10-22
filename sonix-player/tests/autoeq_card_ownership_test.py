from pathlib import Path
import argparse,hashlib,json,os,re,resource,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent);p.add_argument('--reviewed-overlay',type=Path);p.add_argument('--results',type=Path);p.add_argument('--sanitize',action='store_true');p.add_argument('--negative',action='store_true');p.add_argument('--host',action='store_true');p.add_argument('--host-fixture-parent',type=Path);args=p.parse_args()
F=Path(__file__).resolve().parent;ROOT=args.source_root.resolve();O=args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
def function(s,name):
 m=list(re.finditer(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+'+name+r'\([^;]*?\)\s*\{',s,re.M))[-1];end,depth=m.end(),1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'
paths=[O/'src/gui/audio/peqautoeq.c',O/'src/system/device/system.c',O/'src/system/audio/eq.c',ROOT/'src/system/core/config.c']
pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths};source,core,eq,config=[x.read_text(encoding='utf-8') for x in paths]
core_names=('storage_card_revoke_locked','storage_card_publish_locked','storage_card_owned_locked','storage_card_try_admit','storage_card_try_check','storage_open_card_locked','storage_pin_card_locked','storage_pin_card_expected','storage_pin_card_if','storage_pin_card_read_if')
names=('hash_text','has_text','copy_trimmed','profile_source','finish_result_names','parse_index','encode_path','make_profile_url','job_cancelled','job_stale','write_preflight','directory_at','autoeq_directory','owned_temp','write_all','publish_file','read_snapshot','read_cache','stage_filesystem','host_cache_root','internal_stage_root','internal_stage','download_index','commit_index','load_search_results','clear_profile_cache','update_index','commit_profile','download_profile')
cases=('download-profile','rename-fail','write-zero','write-error','file-sync-fail','directory-sync-fail','close-fail','directory-close-fail','write-eintr','write-short','unsupported','cleanup-refusal','old-token','old-commit-token','swap-owned-fd','index-update','internal-type','internal-space','stage-anonymous','stage-unlink-fail','index-too-large','index-stream-too-large','readonly-cached','readonly-profile','readonly-cold-search','readonly-cold-profile','readonly-update','cached-profile-no-rewrite','cache-clear','clear-refusal','directory-symlink','final-symlink','config-fail','parser-oom','parser-compatibility','closing')
variants=[('actual',source,core,False,None)]
if args.host:
 cases=('host-cold-index','host-home-fallback','host-relative','host-traversal','host-symlink','host-tmpfs','host-ramfs','host-home-cache-ram')
if args.negative and not args.host:
 variants += [('refresh-commit-token',source.replace(function(source,'commit_profile'),function(source,'commit_profile').replace('storage_pin_card_if(&job->card)','storage_pin_card_if(&(storage_card_token_t){.epoch=card_epoch})')),core,True,'old-commit-token'),
 ('ignore-ro-write',source,core.replace('if (writable && !sd_writable)','if (false && writable && !sd_writable)'),True,'readonly-profile'),
 ('apply-before-commit',source.replace('if (!commit_profile(job)) {','peq_preset_apply_parsed(job->preset, &job->parsed, true);\n\tif (!commit_profile(job)) {'),core,True,'rename-fail'),
 ('wrong-internal-filesystem',source.replace('(unsigned long)type.f_type == AUTOEQ_UBIFS_MAGIC','true'),core,True,'internal-type'),
 ('no-stage-reserve',re.sub(r'space.f_bavail >= [^;]+','true',source,count=1),core,True,'internal-space'),
 ('follow-parent-symlink',source.replace('O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW','O_DIRECTORY | O_CLOEXEC'),core,True,'directory-symlink'),
 ('ignore-required-rename',source.replace('ok = renameat(directory, temporary, directory, name) == 0;','(void)renameat(directory, temporary, directory, name);'),core,True,'rename-fail'),
 ('ignore-cleanup-failure',source.replace('fprintf(stderr, "autoeq: temporary cleanup failed\\n");','(void)0;'),core,True,'cleanup-refusal'),
 ('rewrite-cached-profile',source.replace('job->cached_profile ? -1 : autoeq_directory(pin, true, true)', 'autoeq_directory(pin, true, true)'),core,True,'cached-profile-no-rewrite'),
 ('bypass-cold-write-preflight',source.replace(function(source,'write_preflight'),function(source,'write_preflight').replace('if (pin < 0)\n\t\treturn false;','if (pin < 0)\n\t\treturn true;')),core,True,'readonly-cold-search')]
if args.host and args.negative:
 variants += [('host-allow-tmpfs',source.replace('kind != 0x01021994u && kind != 0x858458f6u','true'),core,True,'host-tmpfs'),
 ('host-follow-symlink',source.replace('O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW','O_DIRECTORY | O_CLOEXEC'),core,True,'host-symlink'),
 ('host-ignore-home-cache-fs',source.replace('\t\tparent = child;\n\t\tif (!stage_filesystem(parent)) {\n\t\t\tclose(parent);\n\t\t\treturn -1;\n\t\t}', '\t\tparent = child;'),core,True,'host-home-cache-ram')]
resource.setrlimit(resource.RLIMIT_CORE,(0,0));records=[]
with tempfile.TemporaryDirectory(prefix='autoeq61-io-',dir=args.host_fixture_parent) as td:
 t=Path(td);result=(args.results.parent/(args.results.stem+'-logs')) if args.results else t/'logs';result.mkdir(parents=True,exist_ok=True);(t/'actual_eq.c').write_text(eq,encoding='utf-8',newline='\n')
 (t/'reference_parser.inc').write_bytes((F/'autoeq_apo_legacy_parser.inc').read_bytes())
 for name,text,system,negative,only in variants:
  assert not negative or text!=source or system!=core
  types=text[text.index('#define RESULT_MAX'):text.index('static gui_config_t *cfg;')]
  (t/'actual_io_types.inc').write_text(types,encoding='utf-8',newline='\n')
  (t/'actual_storage.inc').write_text(''.join(function(system,n) for n in core_names),encoding='utf-8',newline='\n')
  macros=text[text.index('// No mount reference'):text.index('static bool job_cancelled')]
  (t/'actual_io_functions.inc').write_text(macros+''.join(function(text,n) for n in names),encoding='utf-8',newline='\n')
  binary=t/name;cmd=['cc','-std=gnu11','-O1','-fno-inline','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections','-I'+str(t),'-I'+str(O),'-I'+str(ROOT),'-I'+str(O/'src/system/audio'),'-I'+str(ROOT/'src/system/audio'),str(F/'autoeq_card_ownership_test.c'),str(ROOT/'src/system/core/config.c'),'-Wl,--gc-sections','-pthread','-lm','-o',str(binary)]
  # GCC host's restrict warning treats separate fields of the unchanged
  # result struct as overlapping; target strict compilation remains enabled.
  cmd+=['-Wno-restrict']
  if args.host:cmd+=['-DHOST_BUILD=1']
  if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
  built=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8');assert built.returncode==0,built.stderr
  for case in (only,) if only else cases:
   directory=t/(name+'-'+case);directory.mkdir()
   env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
   ram=None
   if args.host:
    cache=directory/'xdg-cache'
    if case=='host-relative':env['XDG_CACHE_HOME']='relative/cache'
    elif case=='host-traversal':env['XDG_CACHE_HOME']=str(directory/'newprefix/../escape')
    elif case=='host-symlink':
     (directory/'outside').mkdir();cache.symlink_to(directory/'outside',target_is_directory=True);env['XDG_CACHE_HOME']=str(cache)
    elif case=='host-tmpfs':
     ram=tempfile.TemporaryDirectory(prefix='sonix-autoeq61-',dir='/dev/shm');env['XDG_CACHE_HOME']=str(Path(ram.name)/'cache')
    else:env['XDG_CACHE_HOME']=str(cache)
   try:r=subprocess.run([str(binary),str(directory),case],capture_output=True,text=True,encoding='utf-8',timeout=30,env=env)
   finally:
    if ram:
     owned_namespace=Path(ram.name);ram.cleanup();assert not owned_namespace.exists()
   (result/(name+'-'+case+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n')
   if negative:assert r.returncode<0 and 'Assertion' in r.stderr,(name,case,r.returncode,r.stdout,r.stderr)
   else:assert r.returncode==0,(name,case,r.returncode,r.stdout,r.stderr)
   records.append({'variant':name,'case':case,'negative':negative,'returncode':r.returncode,'passed':True});print('PASS',name,case,flush=True)
assert pins=={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
if args.results:args.results.write_text(json.dumps({'passed':True,'sanitized':args.sanitize,'pins':pins,'cases':records},indent=2)+'\n',encoding='utf-8',newline='\n')
