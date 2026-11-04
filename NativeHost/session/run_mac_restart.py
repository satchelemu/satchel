#!/usr/bin/env python3
"""Use only existing private packages. Discard all guest output at the pipe."""
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import threading
import time
import sys
HERE = Path(__file__).resolve().parent
BUILD = Path.home() / 'SatchelBuild'
OUT = Path(os.environ.get('SESSION_MAC_OUT', BUILD / 'session/mac-stop'))
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
while subprocess.run(['pgrep', '-x', 'dmhost'], stdout=subprocess.DEVNULL).returncode == 0:
    print('Waiting for another Mac game host.', flush=True); time.sleep(5)
inputs = {
    'Castlevania': (BUILD / 'games/castlevania/patched_v0', BUILD / 'castlevania/runtime-shaders/cache'),
    'TOTK': (BUILD / 'zelda/rewritten', BUILD / 'zelda/work/shader/cache'),
}
titles = sys.argv[1:] or ['Castlevania', 'TOTK'] + ['Castlevania'] * 5 + ['TOTK', 'Castlevania']
folder = OUT / ('restart-' + str(time.time_ns()))
args = ['nice', '-n', '10', str(OUT / 'mac-stop-probe')]
for name in titles:
    patched, libraries = inputs[name]; package = BUILD / 'anygame/packages' / name
    for part in ['save', 'temp', 'aoc', 'archive']: (folder / name / part).mkdir(parents=True, exist_ok=True)
    for path in [patched / 'main.patched.bin', libraries, package / 'profile.tsv', package / 'romfs.img']: assert path.exists()
    args += [str(patched), str(package), str(libraries), str(folder / name)]
env = {k:v for k,v in os.environ.items() if not k.startswith('DM_')}
env.update(DM_SAVE_SYNC='0', DM_MEMSTAT='memory.tsv', DM_THRCPU='cpu.tsv,1', DM_AUDIO='0', DM_OPMODE='0', DM_PSO_PREBUILD_THREADS='2', DM_PSO_PREPARE='0')
pattern = re.compile(r'^\[session-proof\] phase=([a-z-]+) result=(\d+) guest_threads=(\d+) guest_mappings=(\d+) rss=(\d+) frames=(\d+)$')
process = subprocess.Popen(args, env=env, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
timed_out = threading.Event()
def timeout():
    timed_out.set(); process.kill()
timer = threading.Timer(len(titles) * 90, timeout); timer.start()
rows=[]; metrics=[]; memory=[]; omitted=0
metric_pattern = re.compile(r"^\[session-metrics\] phase=([a-z-]+) threads=(\d+) fds=(\d+)$")
try:
    for line in process.stdout:
        match=pattern.fullmatch(line.strip())
        if match:
            row=dict(zip(['phase','result','guestThreads','guestMappings','rss','frames'],match.groups()))
            for field in list(row)[1:]:row[field]=int(row[field])
            rows.append(row)
            if row['phase'] != 'retire-checkpoint': print(line.strip(),flush=True)
        elif metric_pattern.fullmatch(line.strip()):
            m=metric_pattern.fullmatch(line.strip()); row=dict(phase=m[1],threads=int(m[2]),fds=int(m[3]));metrics.append(row)
            if row['phase'] in ['cold','settled']:print(line.strip(),flush=True)
        elif re.fullmatch(r'\[native-memory\](?: [a-z_]+=\d+)+', line.strip()):
            memory.append({k:int(v) for k,v in re.findall(r'([a-z_]+)=(\d+)',line)})
            print(line.strip(),flush=True)
        elif line.startswith('[session-host-stack] ') and len(line)<500: print(line.strip(),flush=True)
        else:omitted+=1
    process.wait()
finally:
    timer.cancel()
    if process.poll() is None:process.kill();process.wait()
result=dict(titles=titles,exit=process.returncode,timeout=timed_out.is_set(),rows=rows,metrics=metrics,memory=memory,omittedLines=omitted,
            completed=sum(r['phase']=='ended' and r['result']==0 for r in rows))
settled = [r for r in rows if r['phase']=='settled']
settled_metrics = [r for r in metrics if r['phase']=='settled']
if len(settled) >= 2 and len(settled_metrics)==len(titles):
    base = settled[1]['rss']; base_metrics = settled_metrics[1]
    result['warmRSS'] = base; result['finalRSSDelta'] = settled[-1]['rss']-base
    result['bounded'] = all(r['rss'] <= base + 32*1024**2 for r in settled[2:]) and all(
        r['threads'] <= base_metrics['threads']+4 and r['fds'] <= base_metrics['fds']+2 for r in settled_metrics[2:])
else:result['bounded']=False
(folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
(OUT/'restart-results.json').write_text(json.dumps(result,indent=2)+'\n')
print('Completed cycles:',result['completed'],'of',len(titles),flush=True)
print('Resource bounds:', 'PASS' if result['bounded'] else 'FAIL', 'warm RSS:', result.get('warmRSS'), 'final RSS delta:', result.get('finalRSSDelta'), flush=True)
sys.exit(0 if process.returncode==0 and result['completed']==len(titles) and result['bounded'] else 1)
