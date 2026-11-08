#!/usr/bin/env python3
"""Validate native record normalization and rejection boundaries without game logs."""
import json
from pathlib import Path
import runpy
import subprocess
import sys
import time

sys.dont_write_bytecode=True
from oracle import PACKAGES,jobs
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))
# The record harness and the results live in the caller's work folder.
ROOT=DEPENDENCY_PATHS['shader_work']()

def main():
    work=ROOT/'records-full';work.mkdir(parents=True,exist_ok=False)
    scratch=work/'scratch';scratch.mkdir()
    child=subprocess.Popen([ROOT/'record-harness'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    scope,rows=jobs();packs={};counts={'attempted':0,'code_equal':0,'job_equal':0,'rejections':0};differences=[];start=time.monotonic()
    def send(record,key,name):
        (scratch/'record').write_bytes(record)
        request={'input':str(scratch/'record'),'output':str(scratch/'native'),'key':key,'stage':name[:2]}
        if key in scope['FACTS']:request['facts']=scope['FACTS'][key]
        child.stdin.write(json.dumps(request)+'\n');child.stdin.flush()
        return json.loads(child.stdout.readline())
    try:
        for key,(name,index) in sorted(rows.items()):
            if name not in packs:packs[name]=scope['records']((PACKAGES/name).read_bytes())
            record=packs[name][index]
            job,c1=scope['job_for'](key,name,record,str(scratch));code=Path(job.pop('code')).read_bytes()
            response=send(record,key,name);counts['attempted']+=1
            code_ok=response['status']=='ok' and (scratch/'native').read_bytes()==code
            job_ok=response.get('job')==job
            counts['code_equal']+=code_ok;counts['job_equal']+=job_ok
            if not(code_ok and job_ok):differences.append({'key':key,'status':response['status'],'code_equal':code_ok,'job_equal':job_ok})
            for p in scratch.iterdir():p.unlink()
            if counts['attempted']%1000==0:(work/'progress.json').write_text(json.dumps({'counts':counts,'differences':len(differences),'seconds':time.monotonic()-start}))
        # Use only a private real record to test truncation and identity boundaries.
        for altered,badkey in [(record[:0x20],key),(record[:-1],key),(record+b'\0',key),(record,'0'*16), (b'INVALID!'+record[8:],key)]:
            response=send(altered,badkey,name)
            if response['status']!='error':raise RuntimeError('Malformed record accepted')
            counts['rejections']+=1
        result={'counts':counts,'differences':differences,'seconds':time.monotonic()-start}
        (work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({'counts':counts,'differences':len(differences),'seconds':result['seconds']}))
        if differences:raise SystemExit(1)
    finally:
        child.stdin.close();child.wait()
        for p in scratch.iterdir():p.unlink()
        scratch.rmdir()

if __name__=='__main__':main()
