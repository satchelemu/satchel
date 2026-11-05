#!/usr/bin/env python3
"""All three Odyssey geometry records, exact retained source/reflection parity."""
import hashlib
import json
import os
import shutil
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from oracle import functions

WORK=Path.home()/'SatchelBuild/m5'
PACKAGES=Path.home()/'SatchelBuild/games/odyssey/shader/pkg'
CORPUS=PACKAGES.parent/'cache/b-26dc2fa9477c'

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'): raise RuntimeError('Run through guard.py')
    table=Path.home()/'SatchelBuild/games/odyssey/package/Odyssey-1.4.1/dm/shader_map.tsv'
    candidates=[]
    for line in table.read_text().splitlines():
        fields=line.split('\t')
        if len(fields)>3 and fields[2]=='geometry':
            candidates.append(dict(recordFile=fields[0],recordIndex=int(fields[1]),key=fields[3]))
    assert len(candidates)==3 and len({v['key'] for v in candidates})==3, 'Geometry inventory changed'
    record_reader=functions({'records'})['records']
    facts_path=Path.home()/'SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/NX2Apple/dm/games/0100000000010000/shader_facts.tsv'
    facts={}
    if facts_path.exists():
        for line in facts_path.read_text().splitlines():
            if line and not line.startswith('#'):
                key,value=line.split('\t',1);facts[key]=json.loads(value)
    counts={'records':0,'sources':0,'sourceBytesEqual':0,'reflectionValuesEqual':0,'reflectionBytesEqual':0,'runtimeLibraries':0,'computePipelines':0,'publicAPI':0}
    differences=[];timings=[];peaks={};start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='geometry-parity-',dir=WORK) as scratch:
        directory=Path(scratch)
        child=subprocess.Popen([WORK/'geometry-harness'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        try:
            for candidate in candidates:
                key=candidate['key'];raw=record_reader((PACKAGES/candidate['recordFile']).read_bytes())[candidate['recordIndex']]
                path=directory/'record.bin';path.write_bytes(raw)
                base=json.loads((CORPUS/(key+'_b.json')).read_text())
                request={'input':str(path),'output':str(directory),'key':key,'source':base['source'],'pc_paired':base['pc_paired'],'compile':True}
                if key in facts:request['facts']=facts[key]
                child.stdin.write(json.dumps(request)+'\n');child.stdin.flush()
                line=child.stdout.readline()
                if not line:raise RuntimeError('Geometry harness exited before response')
                response=json.loads(line);counts['records']+=1
                if response.get('status')!='ok':differences.append({'key':key,'failure':response});continue
                assert response['publicAPI'];counts['publicAPI']+=1
                peaks[str(response['pid'])]=response['peakRSSBytes']
                timings.append({'key':key,'translationSeconds':response['translateSeconds'],'compileSeconds':response['compileSeconds']})
                actual={p.stem for p in directory.glob('*.metal')}
                expected={p.stem[len(key)+1:] for p in CORPUS.glob(key+'_*.metal')}
                if actual!=expected:differences.append({'key':key,'inventory':{'missing':sorted(expected-actual),'extra':sorted(actual-expected)}})
                for suffix in sorted(actual & expected):
                    a=(directory/(suffix+'.metal')).read_bytes();b=(CORPUS/(key+'_'+suffix+'.metal')).read_bytes()
                    counts['sources']+=1;counts['sourceBytesEqual']+=a==b
                    if a!=b:differences.append({'key':key,'variant':suffix,'kind':'source','actualSHA256':hashlib.sha256(a).hexdigest(),'expectedSHA256':hashlib.sha256(b).hexdigest()})
                    a=(directory/(suffix+'.json')).read_bytes();expected_json=json.loads((CORPUS/(key+'_'+suffix+'.json')).read_bytes())
                    for field in ('chain_metal','chain_compiler'):expected_json.pop(field,None)
                    equal=json.loads(a)==expected_json;exact=a==json.dumps(expected_json,ensure_ascii=False,indent=1).encode()
                    counts['reflectionValuesEqual']+=equal;counts['reflectionBytesEqual']+=exact
                    if not equal or not exact:differences.append({'key':key,'variant':suffix,'kind':'reflection','valuesEqual':equal,'bytesEqual':exact})
                counts['runtimeLibraries']+=len(response['compileSeconds']);counts['computePipelines']+=len(response['compileSeconds'])-1
                for p in directory.iterdir():
                    if p.is_dir():shutil.rmtree(p)
                    else:p.unlink()
                (WORK/'geometry-progress.json').write_text(json.dumps({'counts':counts,'differences':differences},indent=2)+'\n')
        finally:
            child.stdin.close()
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        assert child.returncode==0
    result={'counts':counts,'differences':differences,'timings':timings,'kernelPeakRSSBytesByPID':peaks,'seconds':time.monotonic()-start,'excludedReflectionFields':['chain_metal','chain_compiler']}
    (WORK/'geometry-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'counts':counts,'differences':len(differences),'seconds':result['seconds']}))
    if differences:raise SystemExit(1)

if __name__=='__main__':main()
