"""AutoEQ retained completion: actual functions/queues/mutexes, inert backends.
Run from sonix-player: python3 tests/autoeq_completion_test.py --sanitize --negative
No network, card, device, configuration, GUI teardown or source mutation.
Real-LVGL toast rendering/pointer regression remains tests/run_toast_generation.sh.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent)
p.add_argument('--reviewed-overlay',type=Path,help='Optional unapplied review copies')
p.add_argument('--sanitize',action='store_true');p.add_argument('--negative',action='store_true')
p.add_argument('--results',type=Path);args=p.parse_args()
folder=Path(__file__).resolve().parent;repo=args.source_root.resolve()
def source_file(relative):
 candidate=args.reviewed_overlay/relative if args.reviewed_overlay else None
 return candidate if candidate and candidate.is_file() else repo/relative
# Linux paths are inferred from the actual output directory; no private evidence read.
def function_range(source,name):
 m=list(re.finditer(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+'+name+r'\([^;]*?\)\s*\{',source,re.M))[-1];end,depth=m.end(),1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return m.start(),end

def function(source,name):
 a,b=function_range(source,name);return source[a:b]+'\n'

def mutate(source,name,old,new):
 a,b=function_range(source,name);body=source[a:b];assert body.count(old)==1,(name,old);return source[:a]+body.replace(old,new)+source[b:]

owned=[source_file(n) for n in ('src/gui/audio/peqautoeq.c','src/gui/shell/sx_toast.c','src/gui/shell/toast.c','src/gui/shell/gui.c')]
pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in owned}
source,toast,wrapper,gui=[p.read_text(encoding='utf-8') for p in owned]
names=('path_join','begin_job','end_job','is_busy','hash_text','load_search_results','download_profile','free_job','report_status','worker','start_job','save_and_apply','rebuild_results','service_deferred','peqautoeq_service_completion','job_done','peqautoeq_shutdown_completions','search_left','result_clicked','search_accept','update_database','peqautoeq_open')
cases=[('actual',source,False)]
if args.negative:
 cases += [('free-on-queue-refusal',mutate(source,'worker','(void)gui_post(job_done, NULL);','if (!gui_post(job_done, NULL)) { free_job(job); end_job(); }'),True),
 ('clear-busy-on-worker-publication',mutate(source,'worker','ready_job = job;','ready_job = job; job_busy = false;'),True),
 ('end-newer-toast',mutate(source,'peqautoeq_service_completion','bool owned = toast_busy_end_owned(job->toast_owner);','bool owned = true; sx_toast_busy_end();'),True),
 ('accept-stale-search-reentry',mutate(source,'peqautoeq_service_completion','job->request_generation == search_generation && search_screen','search_screen'),True),
 ('publish-after-closing',mutate(source,'worker','if (job_closing) {','if (false && job_closing) {'),True),
 ('drop-global-apply-after-navigation',mutate(source,'peqautoeq_service_completion','save_and_apply(job, owned);','if (owned) save_and_apply(job, owned);'),True),
 ('continue-after-reentrant-shutdown',mutate(source,'save_and_apply','if (job_closing)\n\t\treturn true;',''),True)]
resource.setrlimit(resource.RLIMIT_CORE,(0,0));records=[]
with tempfile.TemporaryDirectory(prefix='sonix-autoeq-completion-') as temporary:
 temp=Path(temporary)
 owner=''.join(function(toast,n) for n in ('invalidate_input','sx_toast_busy_end','sx_toast_busy_owned','sx_toast_busy_end_owned'))+''.join(function(wrapper,n) for n in ('toast_busy_owned','toast_busy_end_owned'))
 (temp/'actual_toast_owner.inc').write_text(owner, encoding='utf-8', newline='\n')
 (temp/'actual_gui_queues.inc').write_text(function(gui,'gui_post').replace('bool gui_post(', 'static bool actual_gui_post(',1)+function(gui,'gui_post_latest'), encoding='utf-8', newline='\n')
 for name,text,negative in cases:
  types=text[text.index('#define RESULT_MAX'):text.index('static gui_config_t *cfg;')]
  globals=text[text.index('static gui_config_t *cfg;'):text.index('// By hand rather than')]
  (temp/'actual_job_types.inc').write_text(types+globals, encoding='utf-8', newline='\n')
  (temp/'actual_autoeq_functions.inc').write_text(''.join(function(text,n) for n in names), encoding='utf-8', newline='\n')
  binary=temp/name
  cmd=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I'+str(temp),str(folder/'autoeq_completion_test.c'),'-pthread','-o',str(binary)]
  if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  built=subprocess.run(cmd,capture_output=True,text=True, encoding='utf-8');assert built.returncode==0,built.stderr
  env=dict(os.environ)
  if args.sanitize:env['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1'
  result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20,env=env, encoding='utf-8')
  if negative:
   assert result.returncode<0 and ('Assertion' in result.stderr or 'AddressSanitizer' in result.stderr),(name,result.returncode,result.stdout,result.stderr)
  else:assert result.returncode==0,(name,result.stdout,result.stderr)
  records.append({'case':name,'passed':not negative,'broken_variant_rejected':negative,'stdout':result.stdout,'stderr':result.stderr[-1600:]})
  print(('REJECTED' if negative else 'PASS'),name,flush=True)
assert pins=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in owned},'Source changed during fixture run'
if args.results:
 args.results.write_text(json.dumps({'sanitized':args.sanitize,'source_unchanged':True,'source_hashes':pins,'records':records},indent=2)+'\n', encoding='utf-8', newline='\n')
