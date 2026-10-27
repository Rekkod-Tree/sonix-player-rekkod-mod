"""Actual main-loop/common GUI hook routing; module terminal consumers are stubs.
This establishes branch placement and routing, not module ownership/GUI rendering.
Defaults to current repository sources; no baseline/output/Git/device dependency.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,shlex,subprocess,tempfile
HERE=Path(__file__).resolve().parent



def body(text,start):
    end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]


def function(text,name):
    m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\([^;]+?\)\s*\{',text,re.M)
    if not m:raise RuntimeError(name)
    return body(text,m.start())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',type=Path,default=HERE.parent)
    p.add_argument('--reviewed-overlay',type=Path)
    p.add_argument('--cc',default=os.environ.get('CC','cc'))
    p.add_argument('--sanitize',action='store_true')
    args=p.parse_args()
    root=args.source_root.resolve()
    overlay=args.reviewed_overlay.resolve() if args.reviewed_overlay else None
    def source_file(path):
        candidate=overlay/path if overlay else None
        return candidate if candidate and candidate.is_file() else root/path
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    gui=source_file('src/gui/shell/gui.c').read_text(encoding='utf-8');main=source_file('src/main.c').read_text(encoding='utf-8')
    start=main.rindex('\twhile (running) {');loop=body(main,start)
    if loop.count('gui_service_completions();')!=1:raise RuntimeError('One hook per actual loop pass required')
    if loop.index('gui_service_completions();')>loop.index('lv_timer_handler()'):raise RuntimeError('Service must precede LVGL frame/wait')
    tail=main[start+len(loop):]
    if tail.count('gui_shutdown_completions();')!=1 or tail.index('gui_shutdown_completions();')>tail.index('return 0;'):raise RuntimeError('Normal exit shutdown missing')
    hooks='static bool completions_closing;\n'+function(gui,'gui_service_completions')+'\n'+function(gui,'gui_shutdown_completions')
    exit_body=tail[:tail.index('\treturn 0;')]
    source=(HERE/'gui_completion_loop_test.c.in').read_text(encoding='utf-8').replace('@ACTUAL_BRIDGE@',function(gui,'gui_post')+'\n'+function(gui,'gui_post_latest')).replace('@ACTUAL_GUI_COMPLETION_HOOKS@',hooks).replace('@ACTUAL_MAIN_WHILE@',loop).replace('@ACTUAL_MAIN_SHUTDOWN@',exit_body)
    results=[]
    with tempfile.TemporaryDirectory(prefix='gui-completion-branch-') as temp:
        temp=Path(temp);file=temp/'witness.c';file.write_text(source, encoding='utf-8', newline='\n');exe=temp/'witness'
        environment={**os.environ,'TMPDIR':str(temp)}
        cmd=shlex.split(args.cc)+['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread']
        if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
        subprocess.run(cmd+[str(file),'-o',str(exe)],check=True,cwd=temp,env=environment,timeout=60)
        for case in ('blank-paused-noeventfd','awake-branches','late-while-blank','zero-loop-exit'):
            run=subprocess.run([str(exe),case],capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=temp,env=environment,timeout=15)
            if run.returncode:raise RuntimeError(case+' '+run.stderr)
            results.append({'case':case,'passed':True,'stdout':run.stdout,'stderr':run.stderr})
        mutants=[('missing-direct-service',source.replace('\t\tgui_service_completions();','\t\t(void)gui_service_completions;',1),'blank-paused-noeventfd'),
                 ('skip-service-blank',source.replace('\t\tgui_service_completions();','\t\tif(lcd_on) gui_service_completions();',1),'blank-paused-noeventfd'),
                 ('missing-shutdown',source.replace('\tgui_shutdown_completions();','\t(void)gui_shutdown_completions;',1),'zero-loop-exit'),
                 ('missing-checkpoint-shutdown',source.replace('\tlibrary_checkpoint_shutdown();','\t(void)library_checkpoint_shutdown;',1),'zero-loop-exit')]
        for label,mutant,case in mutants:
            if mutant==source:raise RuntimeError('Missing mutation anchor: '+label)
            file=temp/(label+'.c');file.write_text(mutant, encoding='utf-8', newline='\n');exe=temp/label
            subprocess.run(cmd+[str(file),'-o',str(exe)],check=True,cwd=temp,env=environment,timeout=60)
            run=subprocess.run([str(exe),case],capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=temp,env=environment,timeout=15)
            if run.returncode>=0 or 'Assertion' not in run.stderr:raise RuntimeError('Regression not caught at runtime: '+label+' '+run.stderr)
            results.append({'case':label,'passed':True,'runtime_assertion':True,'stderr':run.stderr})
    result={'sanitized':args.sanitize,'results':results,'production_mutated':False,'adb_used':False,'source_sha256':{p:hashlib.sha256(source_file(p).read_bytes()).hexdigest() for p in ['src/main.c','src/gui/shell/gui.c','src/gui/shell/gui.h']}}
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
