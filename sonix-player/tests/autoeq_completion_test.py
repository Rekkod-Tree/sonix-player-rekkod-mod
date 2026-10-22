from pathlib import Path
import argparse,hashlib,json,os,re,resource,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent);p.add_argument('--reviewed-overlay',type=Path);p.add_argument('--results',type=Path);p.add_argument('--sanitize',action='store_true');p.add_argument('--negative',action='store_true');args=p.parse_args()
F=Path(__file__).resolve().parent;ROOT=args.source_root.resolve();O=args.reviewed_overlay.resolve() if args.reviewed_overlay else ROOT
def function_range(s,name):
 m=list(re.finditer(r'^(?:static )?[A-Za-z_][A-Za-z0-9_]*[ \t*]+'+name+r'\([^;]*?\)\s*\{',s,re.M))[-1];end,depth=m.end(),1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return m.start(),end
def function(s,n):
 a,b=function_range(s,n);return s[a:b]+'\n'
def mutate(s,n,a,b):
 x,y=function_range(s,n);f=s[x:y];assert f.count(a)==1,(n,a);return s[:x]+f.replace(a,b)+s[y:]
paths=[O/'src/gui/audio/peqautoeq.c',O/'src/system/device/system.c',ROOT/'src/gui/shell/sx_toast.c',ROOT/'src/gui/shell/toast.c',ROOT/'src/gui/shell/gui.c']
pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
source,core,toast,wrapper,gui=[x.read_text(encoding='utf-8') for x in paths]
names=('begin_job','is_busy','hash_text','job_cancelled','free_job','report_status','worker','admit_and_start','start_job','save_and_apply','rebuild_results','service_deferred','peqautoeq_service_completion','job_done','peqautoeq_shutdown_completions','search_left','result_clicked','search_accept','update_database','peqautoeq_open')
cases=[('actual',source,False)]
if args.negative:
 cases += [('free-on-refused-wake',mutate(source,'worker','(void)gui_post(job_done, NULL);','if (!gui_post(job_done, NULL)) { free_job(job); }'),True),
 ('worker-clears-busy',mutate(source,'worker','ready_job = job;','ready_job = job; job_busy = false;'),True),
 ('wrong-toast-owner',mutate(source,'peqautoeq_service_completion','bool owned = toast_busy_end_owned(job->toast_owner);','bool owned = true; sx_toast_busy_end();'),True),
 ('accept-stale-search',mutate(source,'peqautoeq_service_completion','job->request_generation != search_generation','false'),True),
 ('publish-after-close',mutate(source,'worker','if (job_closing) {','if (false && job_closing) {'),True),
 ('drop-global-apply',mutate(source,'peqautoeq_service_completion','save_and_apply(job, owned);','if (owned) save_and_apply(job, owned);'),True),
 ('reentrant-shutdown',mutate(source,'save_and_apply','if (job_closing)\n\t\treturn true;',''),True),
 ('apply-stale-card',mutate(source,'peqautoeq_service_completion','storage_card_try_check(&job->card) != 0','false'),True),
 ('drop-ready-on-eagain',mutate(source,'peqautoeq_service_completion','if (errno == EAGAIN)\n\t\t\t\treturn;','if (false && errno == EAGAIN)\n\t\t\t\treturn;'),True)]
resource.setrlimit(resource.RLIMIT_CORE,(0,0));records=[]
with tempfile.TemporaryDirectory(prefix='autoeq61-gui-') as td:
 t=Path(td);result=(args.results.parent/(args.results.stem+'-logs')) if args.results else t/'logs';result.mkdir(parents=True,exist_ok=True)
 (t/'actual_toast_owner.inc').write_text(''.join(function(toast,n) for n in ('invalidate_input','sx_toast_busy_end','sx_toast_busy_owned','sx_toast_busy_end_owned'))+''.join(function(wrapper,n) for n in ('toast_busy_owned','toast_busy_end_owned')),encoding='utf-8',newline='\n')
 (t/'actual_gui_queues.inc').write_text(function(gui,'gui_post').replace('bool gui_post(','static bool actual_gui_post(',1)+function(gui,'gui_post_latest'),encoding='utf-8',newline='\n')
 storage='''static pthread_mutex_t card_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t card_epoch;static bool card_epoch_valid,card_epoch_initialized;
static bool card_attached=true,card_released,card_usb_claimed;
static char sd_root[512]="/fixture";
static bool usb_storage_active(void) {return false;}
'''+''.join(function(core,n) for n in ('storage_card_revoke_locked','storage_card_publish_locked','storage_card_owned_locked','storage_card_try_admit','storage_card_try_check'))+'''
static void reset_storage(void) {card_epoch_initialized=card_epoch_valid=card_attached=true;card_released=card_usb_claimed=false;card_epoch=1;}
static void change_owner(void) {storage_card_revoke_locked();storage_card_publish_locked();}
'''
 (t/'actual_gui_storage.inc').write_text(storage,encoding='utf-8',newline='\n')
 for name,text,negative in cases:
  types=text[text.index('#define RESULT_MAX'):text.index('static gui_config_t *cfg;')]
  globals=text[text.index('static gui_config_t *cfg;'):text.index('static bool begin_job')]
  (t/'actual_job_types.inc').write_text(types+globals,encoding='utf-8',newline='\n')
  (t/'actual_autoeq_functions.inc').write_text(''.join(function(text,n) for n in names),encoding='utf-8',newline='\n')
  binary=t/name;cmd=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I'+str(t),str(F/'autoeq_completion_test.c'),'-pthread','-o',str(binary)]
  if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
  b=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8');assert b.returncode==0,b.stderr
  r=subprocess.run([str(binary)],capture_output=True,text=True,encoding='utf-8',timeout=25,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
  (result/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n')
  if negative:assert r.returncode<0 and ('Assertion' in r.stderr or (name=='free-on-refused-wake' and 'ERROR: AddressSanitizer: heap-use-after-free' in r.stderr)),(name,r.returncode,r.stdout,r.stderr)
  else:assert r.returncode==0,(name,r.returncode,r.stdout,r.stderr)
  records.append({'case':name,'negative':negative,'returncode':r.returncode,'passed':True});print('PASS',name,flush=True)
assert pins=={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
if args.results:args.results.write_text(json.dumps({'passed':True,'sanitized':args.sanitize,'pins':pins,'cases':records},indent=2)+'\n',encoding='utf-8',newline='\n')
