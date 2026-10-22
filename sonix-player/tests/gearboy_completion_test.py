"""Whole actual Gearboy module + gbdb OOM controls; Linux/WSL, cc and LVGL headers.
Defaults to the repository containing tests/. Review overlays are optional;
no output baseline, Git, device or persistent file writes are used.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,shlex,subprocess,tempfile

HERE=Path(__file__).resolve().parent



def function(text,name):
    m=re.search(r'^(?:static )?(?:bool|void) '+re.escape(name)+r'\([^;]+?\)\s*\{',text,re.M)
    if not m:raise RuntimeError(name)
    pos=text.index('{',m.start())+1;depth=1
    while depth:
        depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[m.start():pos]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=HERE.parent)
    parser.add_argument('--reviewed-overlay',type=Path)
    parser.add_argument('--lvgl-root',type=Path,help='LVGL directory containing lvgl.h; default SOURCE_ROOT/lvgl or LVGL_ROOT')
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    root=args.source_root.resolve()
    overlay=args.reviewed_overlay.resolve() if args.reviewed_overlay else None
    def source_file(path):
        candidate=overlay/path if overlay else None
        return candidate if candidate and candidate.is_file() else root/path
    lvgl=(args.lvgl_root or Path(os.environ.get('LVGL_ROOT',str(root/'lvgl')))).resolve()
    if not (lvgl/'lvgl.h').is_file():
        parser.error('LVGL headers missing: initialize the repository submodule or pass --lvgl-root')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    code=(HERE/'gearboy_completion_test.c.in').read_text(encoding='utf-8')
    gui=(source_file('src/gui/shell/gui.c')).read_text(encoding='utf-8')
    code=code.replace('@ACTUAL_GUI_POST_FUNCTIONS@',function(gui,'gui_post')+'\n'+function(gui,'gui_post_latest'))
    actual=source_file('src/gui/gearboy/gearboypage.c')
    code=code.replace('@ACTUAL_GEARBOYPAGE_C@',str(actual))
    results=[]
    with tempfile.TemporaryDirectory(prefix='gearboy56-') as tmp:
        tmp=Path(tmp);file=tmp/'test.c';file.write_text(code, encoding='utf-8', newline='\n');exe=tmp/'test'
        cmd=shlex.split(args.cc)+['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections','-DLV_CONF_INCLUDE_SIMPLE=1']
        if overlay:cmd+=['-I'+str(overlay)]
        cmd+=['-I'+str(root),'-I'+str(lvgl.parent),'-I'+str(lvgl),'-I'+str(actual.parent),'-I'+str(source_file('src/system/gearboy/gbdb.h').parent)]
        environment={**os.environ,'TMPDIR':str(tmp)}
        if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
        subprocess.run(cmd+[str(file),'-Wl,--gc-sections','-pthread','-lm','-o',str(exe)],check=True,cwd=tmp,env=environment,timeout=60)
        for case in ('all','closing-active','delete-active'):
            run=subprocess.run([str(exe)]+([] if case=='all' else [case]),capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=tmp,env=environment,timeout=15)
            if run.returncode:raise RuntimeError(case+' '+str(run.returncode)+' '+run.stderr)
            results.append({'case':case,'passed':True,'stdout':run.stdout,'stderr':run.stderr})
        scanner=source_file('src/system/gearboy/gbdb.c')
        scan=(HERE/'gbdb_scan_oom_test.c.in').read_text(encoding='utf-8').replace('@ACTUAL_GBDB_C@',str(scanner))
        file=tmp/'scan.c';file.write_text(scan, encoding='utf-8', newline='\n');scanexe=tmp/'scan'
        subprocess.run(cmd+[str(file),'-Wl,--gc-sections','-pthread','-lm','-o',str(scanexe)],check=True,cwd=tmp,env=environment,timeout=60)
        card=tmp/'card';card.mkdir()
        run=subprocess.run([str(scanexe),str(card)],capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=tmp,env=environment,timeout=15)
        if run.returncode:raise RuntimeError('scanner '+str(run.returncode)+' '+run.stderr)
        results.append({'case':'actual-scanner-oom','passed':True,'stdout':run.stdout,'stderr':run.stderr})
        source=actual.read_text(encoding='utf-8')
        drop='''if (!gui_post(scan_done_cb, NULL)) {
		pthread_mutex_lock(&scan_terminal.lock);
		gbrom_t *lost = scan_terminal.list;
		scan_terminal.list = NULL;
		scan_terminal.ready = false;
		pthread_mutex_unlock(&scan_terminal.lock);
		free(lost);
	}'''
        wrapped='''void *transport = malloc(1);
	if (!transport) {
		free(list);
		return NULL;
	}
	free(transport);
'''
        mutants=[('negative-free-on-rejected-wake',source.replace('gui_post(scan_done_cb, NULL);',drop,1),'all'),
                 ('negative-result-wrapper-oom',source.replace('\tpthread_mutex_lock(&scan_terminal.lock);\n\tif (scan_terminal.closing)',wrapped+'\tpthread_mutex_lock(&scan_terminal.lock);\n\tif (scan_terminal.closing)',1),'all')]
        for name,mutant,case in mutants:
            if mutant==source:raise RuntimeError('Negative seam missing: '+name)
            module=tmp/(name+'.c');module.write_text(mutant, encoding='utf-8', newline='\n')
            file=tmp/(name+'-fixture.c');file.write_text(code.replace(str(actual),str(module)), encoding='utf-8', newline='\n')
            negative=tmp/name
            subprocess.run(cmd+[str(file),'-Wl,--gc-sections','-pthread','-lm','-o',str(negative)],check=True,cwd=tmp,env=environment,timeout=60)
            run=subprocess.run([str(negative)],capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=tmp,env=environment,timeout=15)
            if run.returncode>=0 or 'Assertion' not in run.stderr:raise RuntimeError('Negative not caught at runtime: '+name+' '+run.stderr)
            results.append({'case':name,'passed':True,'runtime_assertion':True,'returncode':run.returncode,'stderr':run.stderr})
        scan_source=scanner.read_text(encoding='utf-8')
        anchor='''if (!collect_dir(&list, root, DIR_GB, false) || !collect_dir(&list, root, DIR_GBC, true)) {
		free(list.items);
		return -ENOMEM;
	}'''
        if anchor not in scan_source:raise RuntimeError('Scanner negative seam missing')
        scanner_mutant=tmp/'scanner-negative.c';scanner_mutant.write_text(scan_source.replace(anchor,anchor.replace('return -ENOMEM;','return 0;'),1), encoding='utf-8', newline='\n')
        file=tmp/'scanner-negative-fixture.c';file.write_text(scan.replace(str(scanner),str(scanner_mutant)), encoding='utf-8', newline='\n')
        negative=tmp/'scanner-negative'
        subprocess.run(cmd+[str(file),'-Wl,--gc-sections','-pthread','-lm','-o',str(negative)],check=True,cwd=tmp,env=environment,timeout=60)
        card=tmp/'card-negative';card.mkdir()
        run=subprocess.run([str(negative),str(card)],capture_output=True,text=True,encoding="utf-8",errors="replace",cwd=tmp,env=environment,timeout=15)
        if run.returncode>=0 or 'Assertion' not in run.stderr:raise RuntimeError('Scanner partial-success regression not caught')
        results.append({'case':'negative-scanner-oom-as-empty','passed':True,'runtime_assertion':True,'returncode':run.returncode,'stderr':run.stderr})
    report={'sanitized':args.sanitize,'results':results,'module_sha256':hashlib.sha256(actual.read_bytes()).hexdigest(),'production_mutated':False,'adb_used':False}
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
