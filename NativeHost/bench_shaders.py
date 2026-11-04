from pathlib import Path
import argparse,json,os,re,subprocess,threading,time
OUT=Path(os.environ['M5_BENCH_OUT']);BUILD=Path.home()/'SatchelBuild';BASE=BUILD/'anygame'
parser=argparse.ArgumentParser();parser.add_argument('--route',choices=['baseline','candidate'],required=True);parser.add_argument('--game',choices=['Castlevania','Odyssey','TOTK'],required=True)
parser.add_argument('--cache',choices=['clean','warm'],required=True)
parser.add_argument('--frame',type=int,default=1800)
a=parser.parse_args();game=a.game;route=a.route; mode=a.cache
assert a.frame>=1200
finalShot='frame-'+str(a.frame)+'.png'
host=Path(os.environ.get('M5_BENCH_HOST',OUT/route/'dmhost'))
# Use the shared build guard's recognized workload name during host measurements.
runner=host.with_name('shader-harness')
if not runner.exists(): os.link(host,runner)
host=runner
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1]/'NativeApp/build_support.py'))['wait']()
package=BASE/'packages'/game
manifest=json.loads((package/'manifest.json').read_text());assert manifest['format']==2
assert not any(line and not line.startswith('#') for line in (package/'dm/shader_map.tsv').read_text().splitlines())
assert not list((package/'msl').rglob('*.metallib'))
work=OUT/'boots'/game/route/mode/str(time.time_ns())
cache=OUT/'cache'/route/game
if mode=='clean': assert not cache.exists(), 'Clean mode requires a new benchmark namespace'
else: assert cache.exists(), 'Warm mode requires the completed clean route'
cache.mkdir(parents=True,exist_ok=True)
for folder in ['save','temp','aoc','shots','empty-msl']:(work/folder).mkdir(parents=True,exist_ok=True)
if game=='TOTK':
    import shutil
    shutil.copytree(BUILD/'host/saves/dmios-final/Documents/save',work/'save',dirs_exist_ok=True)
ctl=work/'ctl.txt';ctl.write_text('')
env={k:v for k,v in os.environ.items() if not k.startswith('DM_')}
env.update(DM_SHADER_EVENTS=str(work/'events.tsv'),DM_PSO_ARCHIVE=str(cache/'pipelines'),DM_PSO_USER=str(cache/'pso_user.tsv'),DM_PSO_HYBRID='0',DM_PSO_MTL4='1',DM_PROFILE=str(package/'profile.tsv'),DM_ROMFS=str(package/'romfs.img'),DM_ROOT=str(work),DM_SAVE=str(work/'save'),DM_TEMP=str(work/'temp'),DM_AOC_DIR=str(work/'aoc'),DM_CTL=str(ctl),DM_FPS_TARGET='30' if game=='TOTK' else '0',DM_RENDER_SCALE='1.0',DM_BOOTPROF='1',DM_SLOWFRAME='1',DM_NVN_TSV=str(work/'frames.tsv'),DM_NVN_TSV_CALLS='0',DM_OPMODE='0',DM_PSO_PREBUILD_THREADS='2',DM_PSO_PREPARE_THREADS='2',DM_PSO_MANIFEST='off',DM_PSO_PREBUILD='0',DM_NVN_SHOT=','.join(map(str,sorted({300,1200,a.frame})))+':'+str(work/'shots/frame-%d.png'),DM_NVN_SHOT_OUT='1280x720')
env.update(DM_NVN_SHMAP=str(package/'dm/shader_map.tsv'),DM_MSL_DIR=str(work/'empty-msl'),DM_PHONE_SHADERS='1',DM_PHONE_SHADER_CACHE=str(cache/'sources'))
assert 'DM_NVN_SHDUMP' not in env
start=time.monotonic();child=subprocess.Popen(['nice','-n','10',str(host),str(BASE/'host-inputs'/game)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
failures=[];red=[];translated=set();compiled=set();frames=[];counts={'omitted':0};reason=[]
# A fixed frame route with no input, private saves and a 300-second failure ceiling.
def watch():
    requested=False;completeAt=None
    while child.poll() is None:
        if (work/'shots'/finalShot).exists() and not requested:
            with ctl.open('a') as f:f.write('shot '+str(work/'shots/ctl.png')+'\n')
            requested=True
        if (work/'shots/ctl.png').exists() and completeAt is None:completeAt=time.monotonic()
        if completeAt and time.monotonic()-completeAt>1:reason.append('both frame checkpoints and DM_CTL capture');break
        if time.monotonic()-start>=300:reason.append('300 second limit');break
        time.sleep(.1)
    if child.poll() is None:
        child.terminate()
        try:child.wait(5)
        except subprocess.TimeoutExpired:child.kill()
watcher=threading.Thread(target=watch);watcher.start()
allowed=['[slowframe]', 'PhoneShader ','PackReference ','shader route B','frame ','DM_CTL: screenshot','[RED]','FATAL','CRASH sig=','UNIMPL ','[bootprof]']
failure=re.compile(r'prepare failed|compile failed|failed to load shader|failed to load: build|failed to create.*pipeline|reflection conversion failed|no library|not in the shader map|no vertex shader|no compute shader|geometry capture entry failed|unexpected argument buffer|offline reference source absent|FATAL|CRASH sig=')
with (work/'host-safe.log').open('w') as log:
    for line in child.stdout:
        if not line.startswith(('[dm]','[nvn]','[mtl]','[slowframe]')) or not any(x in line for x in allowed) or re.search(r'OutputDebugString|hexdump|instruction bytes|\[guest\]|\*x[0-9]|bytes below sp|stack scan',line):counts['omitted']+=1;continue
        line=re.sub(r'layout bytes [0-9a-f]+','layout bytes [omitted]',line).split(' | x19=')[0].rstrip()
        log.write(line+'\n');log.flush()
        if failure.search(line):failures.append(line)
        if '[RED]' in line:red.append(line)
        m=re.search(r'PhoneShader key=([a-f0-9]{16}) stage=(\d+) translated',line)
        if m:translated.add((m[1],int(m[2])))
        m=re.search(r'PackReference name=(\S+) compiled',line)
        if m:compiled.add(m[1])
        m=re.search(r'frame (\d+)',line)
        if m:frames.append(int(m[1]))
child.wait();watcher.join()
shots=[str(x) for x in sorted((work/'shots').glob('*.png'))]
passed=child.returncode in (0,-15,143) and reason==['both frame checkpoints and DM_CTL capture'] and not failures
passed &= bool(translated)
result=dict(game=game,route=route,cacheMode=mode,finalFrame=a.frame,work=str(work),exit=child.returncode,seconds=round(time.monotonic()-start,2),stopReason=reason,frames=frames[-5:],translations=len(translated),stages=sorted(set(s for k,s in translated)),referenceLibraries=len(compiled),shaderFailures=failures,red=red,shots=shots,passed=passed,**counts)
timings={}
for line in (work/'frames.tsv').read_text().splitlines():
    cells=line.split('\t')
    if len(cells)==3 and cells[1].startswith('@'):
        timings.setdefault(int(cells[0]),{})[cells[1]]=float(cells[2])
framesMeasured=[r for f,r in sorted(timings.items()) if f <= a.frame and '@frame_ms' in r]
wall=sum(r['@frame_ms'] for r in framesMeasured)/1000
events=[]
for line in (work/'events.tsv').read_text().splitlines():
    ns,duration,thread,kind=line.split('\t');events.append((int(ns),int(duration),int(thread),kind))
submit={e[2] for e in events if e[3]=='submit-thread'}
stats={}
for kind in sorted({e[3] for e in events}-{'submit-thread'}):
    for lane in ['submit','worker']:
        values=[e[1]/1e6 for e in events if e[3]==kind and (e[2] in submit)==(lane=='submit')]
        stats[kind+'/'+lane]=dict(count=len(values),totalMs=round(sum(values),3),maxMs=round(max(values,default=0),3))
result['timing']=dict(frames=len(framesMeasured),seconds=round(wall,3),over50ms=sum(r['@frame_ms']>50 for r in framesMeasured),maxFrameMs=max(r['@frame_ms'] for r in framesMeasured),events=stats,draws=int(sum(r.get('@draws',0) for r in framesMeasured)))
result['cacheBytes']=sum(p.stat().st_size for p in cache.rglob('*') if p.is_file())
passed &= len(framesMeasured) == a.frame
passed &= all((work/'shots'/('frame-'+str(f)+'.png')).is_file() for f in [300,1200,a.frame])
result['passed']=passed
(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
(OUT/(game+'-'+route+'-'+mode+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
raise SystemExit(0 if passed else 1)
