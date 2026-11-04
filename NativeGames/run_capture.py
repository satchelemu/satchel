#!/usr/bin/env python3
"""Run an existing Mac host with private shader capture and filtered diagnostics."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,threading,time
from heavy import wait_for_slot

def filtered_line(line):
    if not line.startswith(('[dm]','[nvn]','[mtl]')):return None
    # Crash diagnostics dereference registers and print four raw stack words per
    # line. Keep symbolic stacks, but never retain these memory payloads.
    if re.search(r'(?i)(OutputDebugString|hexdump|instruction bytes|\[guest\]| registers |\b(?:[0-9a-f]{8} ){4}|^\[dm\]\s+\*x\d+:|bytes below sp:|^\[dm\]\s+(?:[0-9a-f]{1,16}\s+){3}[0-9a-f]{1,16}\s*$)',line):return None
    line=re.sub(r'layout bytes [0-9a-f]+','layout bytes [omitted]',line)
    return line.split(' | x19=')[0].rstrip()+'\n'

def main():
    p=argparse.ArgumentParser()
    for name in ['host','patched','profile','romfs','tables','output']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--libraries',type=Path)
    p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--label',default='capture')
    p.add_argument('--input-script',default='')
    p.add_argument('--env',action='append',default=[],help='Explicit DM_ diagnostic override as NAME=value')
    p.add_argument('--shots',default='300,900,1500')
    p.add_argument('--shot-size',default='',help='Present-pass canvas size, empty for the drawable size, or raw for the swapchain')
    a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+',a.label):p.error('Invalid run label')
    if not 1 <= a.seconds <= 600:p.error('Run duration must be 1 to 600 seconds')
    root=Path(__file__).resolve().parents[1]
    a.output=a.output.resolve()
    if a.output.is_relative_to(root):p.error('Private output must be outside the repository')
    for name in ['host','patched','profile','romfs','tables']:
        if not getattr(a,name).exists():p.error('Missing input: '+name)
    a.output.mkdir(parents=True,exist_ok=True)
    wait_for_slot(a.output)
    if shutil.disk_usage(a.output).free < 36*1024**3:p.error('Less than 36 GiB free')
    for d in ['capture','run/save','run/temp','run/aoc','msl','logs','shots']:(a.output/d).mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('DM_')}
    env.update(DM_PROFILE=str(a.profile.resolve()),DM_ROMFS=str(a.romfs.resolve()),DM_ROOT=str(a.output/'run'),DM_SAVE=str(a.output/'run/save'),DM_TEMP=str(a.output/'run/temp'),DM_MSL_DIR=str((a.libraries or a.output/'msl').resolve()),DM_NVN_SHMAP=str(a.tables.resolve()/'shader_map.tsv'),DM_NVN_SHDUMP=str(a.output/'capture'),DM_CTL=str(a.output/'run/ctl.txt'),DM_BOOTPROF='1',DM_PSO_PREBUILD_THREADS='2',DM_PSO_RECORD=str(a.output/'capture/pso-record.tsv'))
    if a.shot_size == 'raw':
        env['DM_NVN_SHOT_OUT']='raw'
    elif a.shot_size:
        if not re.fullmatch(r'[1-9][0-9]{0,3}x[1-9][0-9]{0,3}',a.shot_size):p.error('Invalid screenshot size')
        width,height=map(int,a.shot_size.split('x'))
        if width*height>16*1024*1024:p.error('Screenshot exceeds 16 megapixels')
        env['DM_NVN_SHOT_OUT']=a.shot_size
    for assignment in a.env:
        name,separator,value=assignment.partition('=')
        if not separator or not re.fullmatch(r'DM_[A-Z0-9_]+',name):p.error('Expected DM_NAME=value')
        env[name]=value
    if a.input_script:env['DM_KEYS']=a.input_script
    if a.shots:
        if not re.fullmatch(r'\d+(,\d+)*',a.shots):p.error('Screenshot frames must be comma-separated integers')
        env['DM_NVN_SHOT']=a.shots+':'+str(a.output/'shots'/(a.label+'-%d.png'))
    (a.output/'run/ctl.txt').write_text('')
    start=time.monotonic();log=a.output/'logs'/(a.label+'.log')
    child=subprocess.Popen(['nice','-n','10',str(a.host.resolve()),str(a.patched.resolve())],cwd=a.output/'run',env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
    reason=[]
    def monitor():
        while child.poll() is None:
            if time.monotonic()-start >= a.seconds:reason.append('bounded duration reached');break
            if shutil.disk_usage(a.output).free < 35.5*1024**3:reason.append('disk reserve reached');break
            time.sleep(.5)
        if child.poll() is None:
            child.terminate()
            try:child.wait(5)
            except subprocess.TimeoutExpired:child.kill()
    threading.Thread(target=monitor,daemon=True).start()
    omitted=0
    with log.open('w') as f:
        for line in child.stdout:
            safe=filtered_line(line)
            if safe is None:omitted+=1;continue
            f.write(safe);f.flush()
    child.wait()
    summary=dict(exit=child.returncode,seconds=round(time.monotonic()-start,2),stopReason=reason,omittedLines=omitted,capturedControls=len(list((a.output/'capture').glob('*.ctl'))),screenshots=[str(f) for f in sorted((a.output/'shots').glob(a.label+'-*.png'))])
    (a.output/'logs'/(a.label+'.json')).write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary))
    return 0 if reason==['bounded duration reached'] and child.returncode in (0,-15,143) else 1

if __name__=='__main__':raise SystemExit(main())
