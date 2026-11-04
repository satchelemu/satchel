#!/usr/bin/env python3
"""Compile normalized runtime captures with the pinned NX2Apple shader chain."""
from pathlib import Path
import argparse,json,os,re,runpy,shutil,signal,subprocess,threading,time
from heavy import wait_for_slot

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['inputs','output','engine','profiles','tools','toolchain-stamp']:
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--target',choices=['mac','ios','mac,ios'],default='mac')
    a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    for name in ['inputs','output','engine','profiles','tools','toolchain_stamp']:
        setattr(a,name,getattr(a,name).resolve())
    for path in [a.inputs,a.output,a.profiles]:
        if path.is_relative_to(root):p.error('Private shader inputs, outputs and profiles must be outside the repository')
    index=json.loads((a.inputs/'capture-index.json').read_text())
    title=index['titleID']
    if not re.fullmatch(r'[0-9A-F]{16}',title):p.error('Invalid title ID')
    profile=a.profiles/title
    if not (profile/'game.tsv').is_file():p.error('The selected title needs a provisional or reviewed game.tsv')
    a.output.mkdir(parents=True,exist_ok=True)
    wait_for_slot(a.output)
    for name in ['shader_map.tsv','shader_facts.tsv','shader_pairs.tsv','pso_pairs.tsv','pso_manifest.tsv']:
        shutil.copyfile(a.inputs/'tables'/name,profile/name)
    env=runpy.run_path(str(root/'scripts/switch_native.py'))['tool_environment'](a.tools)
    env={k:v for k,v in env.items() if not k.startswith(('DM_','DMSH_')) and k not in ('IOSOUT','XLATE','J','WORK')}
    env.update(DM_GAME=title,DM_GAMES_DIR=str(a.profiles),DM_GAMES_FIRST='1',J='2',MAKEFLAGS='-j2',
               DM_ROOT=str(a.output),DMSH_TARGETS=a.target,DMSH_CAS=str(a.output/'cas'),
               PYTHONDONTWRITEBYTECODE='1',SATCHEL_METAL_TOOLCHAIN=json.loads(a.toolchain_stamp.read_text())['metal_toolchain'])
    for name in ['tmp','work']:(a.output/name).mkdir(exist_ok=True)
    env.update(TMPDIR=str(a.output/'tmp'),WORK=str(a.output/'work'))
    if 'ios' in a.target:env['IOSOUT']=str(a.output/'ios')
    locked=subprocess.run(['nice','-n','10',str(a.tools/'venv/bin/python3'),str(a.engine/'dm/gfx/tools/tool_lock.py'),'--check-tools'],env=env,capture_output=True,text=True)
    (a.output/'tool-lock.log').write_text(locked.stdout+locked.stderr)
    if locked.returncode:p.exit(1,'Pinned toolchain verification failed; see tool-lock.log\n')
    command=['nice','-n','10',str(a.tools/'venv/bin/python3'),str(a.engine/'dm/gfx/tools/own_compilers.py'),str(a.inputs/'pkg'),str(a.output/'cache')]
    start=time.monotonic();start_time_wall=time.time();stop=[]
    child=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',start_new_session=True)
    def monitor():
        while child.poll() is None:
            if shutil.disk_usage(a.output).free < 35.5*1024**3:
                stop.append('disk reserve reached')
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(5)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL)
                return
            time.sleep(.5)
    threading.Thread(target=monitor,daemon=True).start()
    # Compiler diagnostics may echo generated source. Retain metadata and the
    # diagnostic itself, never source context or the generated shader body.
    with (a.output/'compile.log').open('w') as log:
        for line in child.stdout:
            if re.match(r'^\S.*:\d+:\d+: (?:fatal error|error|warning|note):',line) or re.match(
                r'^(?:\[|shaders |shader cache:|variant cache:|shadow-input |alpha test |logic operation |pulling libraries|mesh libraries|library stamp:|c1_full.tsv:|cb_extent.tsv:|Metal |metallib |  \[(?:bad|fail|error)|  queries without an answer|  cannot fill)',line):
                log.write(line);log.flush()
    child.wait()
    reports=list((a.output/'cache').glob('b-*/_b_build.json'))
    current=[r for r in reports if r.stat().st_mtime >= start_time_wall]
    rows=json.loads(current[0].read_text()) if len(current)==1 else []
    failures=[r for r in rows if r.get('st')!='ok']
    ok=child.returncode==0 and not stop and len(rows)==index['shaders'] and not failures
    result=dict(exit=child.returncode,seconds=round(time.monotonic()-start,2),target=a.target,
                shaders=len(rows),failed=len(failures),stopReason=stop,complete=ok,
                report=str(current[0]) if len(current)==1 else None)
    (a.output/'compile-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    return 0 if ok else 1

if __name__=='__main__':raise SystemExit(main())
