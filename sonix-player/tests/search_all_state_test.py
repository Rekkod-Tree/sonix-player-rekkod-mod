"""Test the actual overview state machine without network or a physical player."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--lvgl-root',type=Path,required=True);p.add_argument('--results',type=Path,required=True);p.add_argument('--sanitize',action='store_true')
a=p.parse_args();a.results.mkdir(exist_ok=False);root=Path(__file__).resolve().parents[1];fixture=Path(__file__).with_suffix('.c')
files=[fixture,Path(__file__).resolve(),root/'src/gui/library/searchall.c',root/'src/gui/library/searchall.h',root/'src/gui/library/searchsource.h',root/'src/gui/library/searchsource.c']
pins={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
binary=a.results/'search-all-state-test'
cmd=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-ffunction-sections','-fdata-sections','-DHOST_BUILD=1','-DSONIX_ZVUK=1','-DLV_CONF_INCLUDE_SIMPLE=1','-I'+str(root),'-I'+str(a.lvgl_root),'-I'+str(a.lvgl_root/'lvgl'),str(fixture),str(root/'src/gui/library/searchsource.c'),'-Wl,--gc-sections','-pthread','-lm','-o',str(binary)]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
r=subprocess.run(cmd,text=True,capture_output=True,timeout=120);(a.results/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(binary)],text=True,capture_output=True,timeout=60);(a.results/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
assert all(hashlib.sha256(Path(n).read_bytes()).hexdigest()==h for n,h in pins.items())
(a.results/'result.json').write_text(json.dumps(dict(passed=True,sanitized=a.sanitize,sources=pins,command=cmd),indent=2)+'\n')
print(r.stdout.strip())
