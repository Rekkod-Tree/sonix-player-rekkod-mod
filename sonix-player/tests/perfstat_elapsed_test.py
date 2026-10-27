"""Check the production elapsed histogram with deterministic and tail samples.

python3 tests/perfstat_elapsed_test.py
No device input, global settings, timing assumptions or production mutation.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]
source = (base / 'src/system/device/perfstat.c').read_text()

def function(name):
    m = re.search(r'^static [^\n]+\b' + name + r'\([^;]+?\)\s*\{', source, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'

types = source[source.index('#define PERF_TIME_BINS'):source.index('static time_stat_t pass_elapsed')]
test = '#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n' + types
test += function('time_stat_add') + function('time_stat_percentile')
test += r'''
int main(void) {
 time_stat_t s={0};
 assert(time_stat_percentile(&s,95)==0);
 time_stat_add(&s,0);time_stat_add(&s,1000);time_stat_add(&s,1001);
 assert(s.count==3 && s.total==2001 && s.maximum==1001);
 assert(time_stat_percentile(&s,50)==1000 && time_stat_percentile(&s,95)==2000);
 s=(time_stat_t){0};
 for(int i=0;i<1024;i++)time_stat_add(&s,1000);
 for(int i=0;i<1024;i++)time_stat_add(&s,16001);
 assert(s.count==2048 && time_stat_percentile(&s,95)==17000 && s.over16==1024);
 assert(s.over33==0 && s.over100==0);
 s=(time_stat_t){0};
 time_stat_add(&s,16000);time_stat_add(&s,33333);time_stat_add(&s,100000);
 assert(s.over16==2 && s.over33==1 && s.over100==0);
 time_stat_add(&s,100001);time_stat_add(&s,255001);time_stat_add(&s,9999999999ULL);
 assert(s.maximum==9999999999ULL && time_stat_percentile(&s,99)==s.maximum);
 assert(s.over100==3);
 puts("PASS elapsed histogram: all samples, millisecond upper bounds, thresholds, long blocking tail, 64-bit durations");
}
'''
with tempfile.TemporaryDirectory(prefix='sonix-perf-time-') as temporary:
    c = Path(temporary)/'test.c'
    binary = Path(temporary)/'test'
    c.write_text(test)
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('CFLAGS','')),str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
