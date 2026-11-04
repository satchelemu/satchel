#!/usr/bin/env python3
"""Bounded Mac capture, normalize, compile and rerun loop for any title profile."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['host','patched','profile','romfs','profiles','output','engine','tools','toolchain-stamp']:
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--title',required=True)
    p.add_argument('--rounds',type=int,default=4)
    p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--input-script',default='')
    a=p.parse_args()
    if not 1<=a.rounds<=12:p.error('Use 1 to 12 rounds')
    root=Path(__file__).resolve().parent
    a.output=a.output.resolve()
    if a.output.is_relative_to(root.parent):p.error('Private output must be outside the repository')
    a.output.mkdir(parents=True,exist_ok=True);(a.output/'logs').mkdir(exist_ok=True)
    steps=[];stamp=time.strftime('%Y%m%d-%H%M%S');prior=None;start=time.monotonic()
    state=a.output/'capture-loop-state.json'
    def save(status,stage):
        state.write_text(json.dumps(dict(status=status,stage=stage,seconds=round(time.monotonic()-start,2),steps=steps),indent=2)+'\n')
    def run(stage,args):
        save('running',stage);begin=time.monotonic()
        with (a.output/'logs'/(stage+'.driver.log')).open('w') as log:
            result=subprocess.run([sys.executable,*map(str,args)],stdout=log,stderr=subprocess.STDOUT)
        steps.append(dict(stage=stage,exit=result.returncode,seconds=round(time.monotonic()-begin,2)))
        if result.returncode:raise RuntimeError(stage+' failed; see its driver log')
    try:
        for number in range(a.rounds):
            label=f'runtime-{stamp}-{number}'
            run(label,[root/'run_capture.py','--host',a.host,'--patched',a.patched,'--profile',a.profile,'--romfs',a.romfs,
                       '--tables',a.profiles/a.title,'--output',a.output,'--libraries',a.output/'runtime-shaders/cache',
                       '--seconds',a.seconds,'--label',label,'--input-script',a.input_script])
            inputs=a.output/'capture-inputs'/label
            run(label+'-normalize',[root/'capture_shaders.py',a.output/'capture',inputs,'--engine',a.engine,'--title',a.title,
                                   '--profiles',a.profiles,'--require-pairs'])
            digest=hashlib.sha256(b''.join(f.read_bytes() for f in sorted((inputs/'tables').glob('*.tsv')))).hexdigest()
            if digest==prior:
                save('awaiting visual review','capture coverage stable for this run and input route')
                return 0
            if number==a.rounds-1:
                save('needs another iteration','new shader coverage at the round limit; last inputs are not compiled')
                return 2
            run(label+'-compile',[root/'compile_capture.py','--inputs',inputs,'--output',a.output/'runtime-shaders','--engine',a.engine,
                                 '--profiles',a.profiles,'--tools',a.tools,'--toolchain-stamp',a.toolchain_stamp,'--target','mac'])
            prior=digest
    except Exception as error:
        save('blocked',str(error));print(str(error),file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
