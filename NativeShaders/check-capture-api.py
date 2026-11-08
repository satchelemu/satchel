#!/usr/bin/env python3
"""Translate the existing Castlevania capture using the title-independent API."""
import base64
import collections
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import runpy
import tempfile
import time
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

# The API harness (build-api.py) and the results live in the caller's work folder.
WORK=DEPENDENCY_PATHS['shader_work']()
CAPTURE=Path.home()/'SatchelBuild/castlevania/capture-inputs/opening-gameplay'
CORPUS=Path.home()/'SatchelBuild/castlevania/runtime-shaders/cache/b-26dc2fa9477c'

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):raise RuntimeError('Run through guard.py')
    packs={};rows={};facts={};pairs=collections.defaultdict(set)
    for path in (CAPTURE/'pkg').glob('*.bin'):
        data=path.read_bytes();records=[];offset=0
        while offset<len(data):
            assert data[offset:offset+8]==b'BTCDBTCD'
            ctl,code=struct.unpack_from('<II',data,offset+0x28);length=0x38+ctl+code
            assert length<=len(data)-offset
            records.append(data[offset:offset+length]);offset+=length
        packs[path.name]=records
    for line in (CAPTURE/'tables/shader_map.tsv').read_text().splitlines():
        c=line.split('\t')
        if len(c)>4 and c[0].endswith('.bin'):rows[c[3]]=(c[0],int(c[1]))
    for line in (CAPTURE/'tables/shader_facts.tsv').read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            key,value=line.split('\t',1);facts[key]=json.loads(value)
    for line in (CAPTURE/'tables/shader_pairs.tsv').read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            v,p=line.split();pairs[v].add(p)
    def inputs(key):
        name,index=rows[key];record=packs[name][index];code=record[0x38:]
        return {i for i in range(32) if struct.unpack_from('<I',code,0x30+4*(6+(i>>2)))[0]>>((i&3)*8)&0xff}
    child=subprocess.Popen([WORK/'api-macosx/api-harness'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    counts=collections.Counter();differences=[];started=time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix='capture-api-',dir=WORK) as temp:
            directory=Path(temp);path=directory/'record.bin';output=directory/'result.json'
            for key,(name,index) in sorted(rows.items()):
                path.write_bytes(packs[name][index]);wanted=set().union(*(inputs(p) for p in pairs[key])) if pairs[key] else set()
                request=dict(input=str(path),output=str(output),key=key,stage=name[:2],wanted=sorted(wanted))
                if key in facts:request['facts']=facts[key]
                child.stdin.write(json.dumps(request)+'\n');child.stdin.flush()
                response=child.stdout.readline()
                if not response:raise RuntimeError('Capture API harness exited')
                status=json.loads(response);counts['attempted']+=1
                if status['status']!='ok':differences.append(dict(key=key,status=status));continue
                result=json.loads(output.read_text());counts['translated']+=1;counts['sources']+=len(result['sources'])
                expected=(CORPUS/(key+'_b.metal')).read_bytes();actual=result['sources']['b'].encode()
                if expected==actual:counts['baseBytesEqual']+=1
                else:differences.append(dict(key=key,kind='base',expectedSHA256=hashlib.sha256(expected).hexdigest(),actualSHA256=hashlib.sha256(actual).hexdigest()))
                reference=json.loads((CORPUS/(key+'_b.json')).read_text())
                for field in ['chain_compiler','chain_metal']:reference.pop(field,None)
                reference.update(source='native',pc_paired='')
                reflection=base64.b64decode(result['reflectionJSON'])
                if json.loads(reflection)==reference:counts['reflectionValuesEqual']+=1
                else:differences.append(dict(key=key,kind='reflection',fields=sorted(k for k in set(reference)|set(json.loads(reflection)) if reference.get(k)!=json.loads(reflection).get(k))))
                if reflection==json.dumps(reference,ensure_ascii=False,indent=1).encode():counts['reflectionBytesEqual']+=1
    finally:
        child.stdin.close()
        try:child.wait(timeout=10)
        except subprocess.TimeoutExpired:child.kill();child.wait()
    result=dict(counts=counts,differences=differences,seconds=time.monotonic()-started,coverage='198 previously captured shaders, not whole-game coverage')
    (WORK/'capture-api-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(counts=counts,differences=len(differences),seconds=result['seconds'])))
    if differences or child.returncode:raise SystemExit(1)

if __name__=='__main__':main()
