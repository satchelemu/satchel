from pathlib import Path
import argparse,json,os,re,subprocess,threading,time
OUT=Path(os.environ['M5_BENCH_OUT']);BUILD=Path.home()/'SatchelBuild';BASE=BUILD/'anygame'
parser=argparse.ArgumentParser();parser.add_argument('--route',choices=['baseline','candidate'],required=True);parser.add_argument('--game',choices=['Castlevania','Odyssey','TOTK'],required=True)
parser.add_argument('--cache',choices=['clean','warm','relist'],required=True)
# relist: a new namespace holding the completed clean route's recorded list, sources and capture but no pipeline
# archive (an OS update or a cache namespace bump). --capture records first-use shaders as the app does;
# --prewarm builds recorded pipelines before the first frame (-SatchelPipelinePrewarm YES) and needs it.
parser.add_argument('--capture',action='store_true')
parser.add_argument('--prewarm',action='store_true')
parser.add_argument('--prewarm-ms',type=int,default=3000)
parser.add_argument('--pipeline-mode',choices=['exact','async','bounded','skip'],default='exact')
parser.add_argument('--pipeline-budget-ms',type=float,default=4)
parser.add_argument('--frame',type=int,default=1800)
a=parser.parse_args();game=a.game;route=a.route; mode=a.cache
assert a.frame>=1200
assert 0 <= a.pipeline_budget_ms <= 16
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
assert not a.prewarm or a.capture, 'The prewarm prepares recorded shaders from the capture'
if mode=='clean': assert not cache.exists(), 'Clean mode requires a new benchmark namespace'
elif mode=='relist':
    import shutil
    assert cache.exists() and (cache/'pso_user.tsv').exists(), 'Relist mode requires the completed clean route'
    source,cache=cache,OUT/'cache'/route/(game+'-relist')
    assert not cache.exists(), 'Relist mode requires a new namespace'
    shutil.copytree(source,cache,ignore=lambda folder,names:['pipelines'] if Path(folder)==source else [])
else: assert cache.exists(), 'Warm mode requires the completed clean route'
cache.mkdir(parents=True,exist_ok=True)
for folder in ['save','temp','aoc','shots','empty-msl']:(work/folder).mkdir(parents=True,exist_ok=True)
if game=='TOTK':
    import shutil
    shutil.copytree(BUILD/'host/saves/dmios-final/Documents/save',work/'save',dirs_exist_ok=True)
ctl=work/'ctl.txt';ctl.write_text('')
env={k:v for k,v in os.environ.items() if not k.startswith('DM_')}
env.update(DM_PSO_SKIP_STATS=str(work/'pipeline-skips.tsv'),DM_SHADER_EVENTS=str(work/'events.tsv'),DM_PSO_ARCHIVE=str(cache/'pipelines'),DM_PSO_USER=str(cache/'pso_user.tsv'),DM_PSO_HYBRID='0',DM_PSO_MTL4='1',DM_PROFILE=str(package/'profile.tsv'),DM_ROMFS=str(package/'romfs.img'),DM_ROOT=str(work),DM_SAVE=str(work/'save'),DM_TEMP=str(work/'temp'),DM_AOC_DIR=str(work/'aoc'),DM_CTL=str(ctl),DM_FPS_TARGET='30' if game=='TOTK' else '0',DM_RENDER_SCALE='1.0',DM_BOOTPROF='1',DM_SLOWFRAME='1',DM_NVN_TSV=str(work/'frames.tsv'),DM_NVN_TSV_CALLS='0',DM_OPMODE='0',DM_PSO_PREBUILD_THREADS='2',DM_PSO_PREPARE_THREADS='2',DM_PSO_MANIFEST='off',DM_PSO_PREBUILD='0',DM_NVN_SHOT=','.join(map(str,sorted({300,1200,a.frame})))+':'+str(work/'shots/frame-%d.png'),DM_NVN_SHOT_OUT='1280x720')
env.update(DM_PSO_ASYNC='1' if a.pipeline_mode in {'async','bounded','skip'} else '0',DM_PSO_SKIP='1' if a.pipeline_mode=='skip' else '0',DM_PSO_BUDGET_MS=str(a.pipeline_budget_ms),DM_PSO_HYBRID='1' if a.pipeline_mode in {'async','bounded','skip'} else '0',DM_PSO_AHEAD='1' if a.pipeline_mode=='bounded' else '0')
env.update(DM_NVN_SHMAP=str(package/'dm/shader_map.tsv'),DM_MSL_DIR=str(work/'empty-msl'),DM_PHONE_SHADERS='1',DM_PHONE_SHADER_CACHE=str(cache/'sources'))
if a.capture:
    (cache/'shader-capture').mkdir(exist_ok=True); env['DM_NVN_SHDUMP']=str(cache/'shader-capture')
if a.prewarm:
    # PipelineSettings.m's -SatchelPipelinePrewarm YES: the engine's blocking prewarm of the seed and recorded lists.
    del env['DM_PSO_MANIFEST']; env.update(DM_PSO_PREPARE='1',DM_SATCHEL_PREWARM='1',DM_PSO_PREPARE_MS=str(a.prewarm_ms))
if os.environ.get('M5_CAPTURE_REQUESTS'): env['DM_SHADER_REQUEST_CAPTURE']=os.environ['M5_CAPTURE_REQUESTS']
if os.environ.get('M5_SKIP_CAPTURE'): env['DM_PSO_SKIP_CAPTURE']=str(work/'shots')
if os.environ.get('M5_EXTRA_FRAMES'):
    env['DM_NVN_SHOT']=os.environ['M5_EXTRA_FRAMES']+','+env['DM_NVN_SHOT']
assert ('DM_NVN_SHDUMP' in env)==a.capture
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
allowed=['[slowframe]', 'PhoneShader ','pipeline prepare','pipeline prewarm','PackReference ','shader route B','frame ','DM_CTL: screenshot','[RED]','FATAL','CRASH sig=','UNIMPL ','[bootprof]']
failure=re.compile(r'prepare failed|compile failed|failed to load shader|failed to load: build|failed to create.*pipeline|reflection conversion failed|no library|not in the shader map|no vertex shader|no compute shader|geometry capture entry failed|unexpected argument buffer|offline reference source absent|FATAL|CRASH sig=')
with (work/'host-safe.log').open('w') as log:
    for line in child.stdout:
        if not line.startswith(('[dm]','[nvn]','[mtl]','[slowframe]')) or not any(x in line for x in allowed) or re.search(r'OutputDebugString|hexdump|instruction bytes|\[guest\]|\*x[0-9]|bytes below sp|stack scan',line):counts['omitted']+=1;continue
        line=re.sub(r'layout bytes [0-9a-f]+','layout bytes [omitted]',line).split(' | x19=')[0].rstrip()
        log.write(line+'\n');log.flush()
        # The prewarm's summary counts recorded keys it could not prepare ('not in the shader map'); that is not a failure.
        if failure.search(line) and 'pipeline prewarm done' not in line:failures.append(line)
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
result=dict(pid=child.pid,pipelineMode=a.pipeline_mode,capture=a.capture,prewarm=a.prewarm,pipelineBudgetMs=a.pipeline_budget_ms,game=game,route=route,cacheMode=mode,finalFrame=a.frame,work=str(work),exit=child.returncode,seconds=round(time.monotonic()-start,2),stopReason=reason,frames=frames[-5:],translations=len(translated),stages=sorted(set(s for k,s in translated)),referenceLibraries=len(compiled),shaderFailures=failures,red=red,shots=shots,passed=passed,**counts)
timings={}
for line in (work/'frames.tsv').read_text().splitlines():
    cells=line.split('\t')
    # Since the f8a5618d pin some probes are written with no value (@use_skip); they carry no timing.
    if len(cells)==3 and cells[1].startswith('@') and cells[2]:
        timings.setdefault(int(cells[0]),{})[cells[1]]=float(cells[2])
for line in (work/'pipeline-skips.tsv').read_text().splitlines() if (work/'pipeline-skips.tsv').exists() else []:
    frame, count = map(int, line.split('\t'))
    timings.setdefault(frame, {})['@pipeline_skips'] = count
framesMeasured=[r for f,r in sorted(timings.items()) if f <= a.frame and '@frame_ms' in r]
wall=sum(r['@frame_ms'] for r in framesMeasured)/1000
events=[]
for line in (work/'events.tsv').read_text().splitlines():
    ns,duration,thread,kind=line.split('\t');events.append((int(ns),int(duration),int(thread),kind))
submit={e[2] for e in events if e[3]=='submit-thread'}
stats={}
for kind in sorted({e[3] for e in events}-{'submit-thread','frame'}):
    for lane in ['submit','worker']:
        values=[e[1]/1e6 for e in events if e[3]==kind and (e[2] in submit)==(lane=='submit')]
        stats[kind+'/'+lane]=dict(count=len(values),totalMs=round(sum(values),3),maxMs=round(max(values,default=0),3))
result['timing']=dict(frames=len(framesMeasured),seconds=round(wall,3),over50ms=sum(r['@frame_ms']>50 for r in framesMeasured),maxFrameMs=max(r['@frame_ms'] for r in framesMeasured),events=stats,draws=int(sum(r.get('@draws',0) for r in framesMeasured)))
# Merge nested first-use spans so pipeline wrapper events are not counted twice.
# These are wait episodes across the route, not frame counts or GPU stalls.
spans=sorted((ns,ns+duration) for ns,duration,thread,kind in events if thread in submit and kind in
    {'prepare-use','library-use','pipeline-use','pipeline-wait','pipeline-required-wait'})
merged=[]
for lo,hi in spans:
    if merged and lo <= merged[-1][1]: merged[-1][1]=max(merged[-1][1],hi)
    else: merged.append([lo,hi])
result['timing']['firstUseHitchFramesOver50ms']=sum(r['@frame_ms']>50 and (r.get('@lib_new',0)>0 or r.get('@pso_new',0)>0) for r in framesMeasured)
result['timing']['firstUseWaitsOverBudget']=sum(hi-lo>a.pipeline_budget_ms*1e6 for lo,hi in merged)
result['timing']['firstUseWaitUnionMs']=round(sum(hi-lo for lo,hi in merged)/1e6,3)
skipped={f:int(r.get('@pipeline_skips',0)) for f,r in sorted(timings.items()) if f<=a.frame and r.get('@pipeline_skips',0)>0}
result['timing']['skippedDraws']=sum(skipped.values())
result['timing']['framesWithSkips']=len(skipped)
result['timing']['skippedFrameCounts']=skipped
# Frame zero has no preceding interval, but its omitted draws still count.
longest=run=0;previous=-2
for frame in skipped:
    run=run+1 if frame==previous+1 else 1;longest=max(longest,run);previous=frame
result['timing']['longestSkipRunFrames']=longest
# Per-frame stall attribution (stall_attribution.py, shared with phone trial files).
import sys; sys.path.insert(0,str(Path(__file__).resolve().parent))
import stall_attribution
result['timing']['stalls']=stall_attribution.summarize(timings,events,a.frame)
result['cacheBytes']=sum(p.stat().st_size for p in cache.rglob('*') if p.is_file())
passed &= len(framesMeasured) == a.frame
passed &= all((work/'shots'/('frame-'+str(f)+'.png')).is_file() for f in [300,1200,a.frame])
result['passed']=passed
(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
(OUT/(game+'-'+route+'-'+mode+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
raise SystemExit(0 if passed else 1)
