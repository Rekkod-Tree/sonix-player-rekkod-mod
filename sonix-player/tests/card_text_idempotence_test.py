"""Common card/status setter regression linked against actual matching LVGL objects.

From sonix-player after make host UI_TEST=1:
  python3 tests/card_text_idempotence_test.py --lvgl-root .
  python3 tests/card_text_idempotence_test.py --lvgl-root . --sanitize
No device, configuration, network or source mutation. Glyph metrics and Wi-Fi
snapshots are deterministic fixtures; physical font/pixel acceptance is separate.
The app/harness can be sanitized; the existing LVGL objects are not rebuilt here.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,resource,subprocess,tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--repo',type=Path,help='sonix-player root; defaults to this test parent')
parser.add_argument('--lvgl-root',required=True,type=Path,help='Matching sonix-player build root with lvgl/ and build_host/lvgl/')
parser.add_argument('--card-source',type=Path,help='Optional unapplied review copy of sx_card.c')
parser.add_argument('--wifi-source',type=Path,help='Optional unapplied review copy of wifisettings.c')
parser.add_argument('--sanitize',action='store_true')
parser.add_argument('--results',type=Path)
args=parser.parse_args()
folder=Path(__file__).resolve().parent
repo=(args.repo or folder.parent).resolve();host=args.lvgl_root.resolve()
paths=[(args.card_source or repo/'src/gui/shell/sx_card.c').resolve(),(args.wifi_source or repo/'src/gui/wireless/wifisettings.c').resolve(),repo/'src/gui/shell/sx_styles.c']
pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
card,wifi,styles=[p.read_text() for p in paths]
objects=sorted((host/'build_host/lvgl').rglob('*.o'))
assert objects and (host/'lvgl/lvgl.h').is_file(),'Build the matching LVGL host tree first.'

def function(source,name):
    match=re.search(r'^(?:static )?[^;\n]+\b'+name+r'\([^;]*?\)\s*\{',source,re.MULTILINE)
    assert match,name
    end,depth=match.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]+'\n'

def functions(card_source,wifi_source):
    parts=re.search(r'typedef struct \{.*?\} card_parts_t;',card_source,re.DOTALL)
    assert parts
    result=parts.group()+'\n'+function(styles,'sx_set_text')
    for name in ('parts_of','head_parts','sx_card_title','sx_card_subtitle','sx_card_value'):
        result+=function(card_source,name)
    return result+function(wifi_source,'refresh_status_label')

actual=functions(card,wifi)
# Generate a concrete negative control from the current source, not old Git/output
# files: put the redundant text writes and subtitle style rebinding back.
negative=actual
for member in ('title','subtitle','value'):
    before=f'sx_set_text(p->{member}, text ? text : "");'
    assert negative.count(before)==1,('Missing fixed setter',member)
    negative=negative.replace(before,before.replace('sx_set_text','lv_label_set_text'))
assert negative.count('sx_set_text(status_label,')==3
negative=negative.replace('sx_set_text(status_label,','lv_label_set_text(status_label,')
condition='if (created || lv_obj_get_style_text_font(p->subtitle, 0) != font)'
assert negative.count(condition)==1
negative=negative.replace(condition,'if (true)')
resource.setrlimit(resource.RLIMIT_CORE,(0,0));records=[]
with tempfile.TemporaryDirectory(prefix='sonix-card-text-') as temporary:
    temporary=Path(temporary)
    for name,source,fixed in [('actual',actual,True),('redundant-setters-negative-control',negative,False)]:
        (temporary/'actual_functions.inc').write_text(source)
        binary=temporary/name
        command=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-DTEST_PATCHED='+str(int(fixed)),'-DLV_CONF_INCLUDE_SIMPLE=1','-I'+str(repo),'-I'+str(host),'-I'+str(host/'lvgl'),'-I'+str(temporary)]
        if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        command+=[str(folder/'card_text_idempotence_test.c'),*[str(p) for p in objects],'-Wl,--wrap=lv_malloc','-Wl,--wrap=lv_realloc','-Wl,--wrap=lv_label_set_text','-Wl,--wrap=lv_obj_add_style','-Wl,--wrap=lv_obj_remove_style','-Wl,--wrap=lv_obj_invalidate','-lSDL2','-lfreetype','-lm','-lpthread','-ldl','-o',str(binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        environment=dict(os.environ)
        if args.sanitize:environment['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1'
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,env=environment)
        assert result.returncode==0,(name,result.stdout,result.stderr)
        print(result.stdout,end='')
        records.append({'case':name,'passed':True,'actual_lvgl':True,'evidence':result.stdout})
assert pins=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'Source changed during test.'
if args.results:
    args.results.write_text(json.dumps({'source_unchanged':True,'source_hashes':pins,'lvgl_objects_instrumented':False,'sanitizers':args.sanitize,'records':records},indent=2)+'\n')
