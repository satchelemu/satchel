#!/usr/bin/env python3
"""Read existing private DMROMFS1 ranges, never extract or duplicate the image."""
import collections
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import runpy
import subprocess
import time
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

# The general harness (build-api.py --general) and the results live in the caller's work folder.
WORK = DEPENDENCY_PATHS['shader_work']()
# The Zelda profile of the pinned engine, as oracle.py reads it (SATCHEL_SHADER_PROFILE overrides).
PROFILE = Path(os.environ.get('SATCHEL_SHADER_PROFILE', DEPENDENCY_PATHS['engine']() / 'dm/games/0100F2C0115B6000'))
IMAGE = Path.home()/'SatchelBuild/oneapp/transfer/Documents/PreparedGames/TOTK-1.4.3-2/romfs.img'

def files():
    with IMAGE.open('rb') as stream:
        header=stream.read(32)
        if header[:8]!=b'DMROMFS1':raise RuntimeError('Invalid image magic')
        offset,size,count=struct.unpack_from('<3Q',header,8)
        if count>1000000 or size>128*1024**2 or offset+size!=IMAGE.stat().st_size:raise RuntimeError('Invalid image index')
        stream.seek(offset);data=stream.read(size)
    result={};pending=[(0,'')];seen=set()
    while pending:
        i,parent=pending.pop()
        if i in seen or not 0<=i<count:raise RuntimeError('Invalid image tree')
        seen.add(i)
        first,length,name_offset,name_length,kind=struct.unpack_from('<QQIHB',data,i*24)
        name=data[count*24+name_offset:count*24+name_offset+name_length].decode()
        path=parent+'/'+name if name else parent
        if kind==1:result[path]=dict(path=str(IMAGE),offset=first,count=length)
        else:
            if first+length>count:raise RuntimeError('Invalid child range')
            pending.extend((j,path) for j in range(first,first+length))
    if len(seen)!=count:raise RuntimeError('Unreachable image entries')
    return result,hashlib.sha256(header+data).hexdigest()

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):raise RuntimeError('Run through guard.py')
    parser=argparse.ArgumentParser();parser.add_argument('--label',default='discovery-full');parser.add_argument('--all-effects',action='store_true')
    args=parser.parse_args()
    if Path(args.label).name!=args.label or args.label in ('.','..'):raise ValueError('Expected one label')
    index,identity=files();expected=collections.defaultdict(set);all_keys=set();main_keys=set()
    for line in (PROFILE/'shader_map.tsv').read_text().splitlines():
        columns=line.split('\t')
        if len(columns)>4 and columns[0].endswith('.bin'):
            key,origin=columns[3:5];all_keys.add(key)
            if origin.startswith('main'):main_keys.add(key)
            else:
                outer=origin.split('#',1)[0]
                while outer not in index and outer not in ('','/'):
                    outer=outer.rsplit('/',1)[0]
                expected[outer].add(key)
    missing_paths=set(expected)-set(index)
    if missing_paths:raise RuntimeError('Reference archive paths missing from input image')
    if args.all_effects:
        for name in index:
            if name.startswith('/Effect/') and name.endswith('.byml.zs'):expected.setdefault(name,set())
    dictionary=index['/Pack/ZsDic.pack.zs'];output=WORK/args.label;output.mkdir(parents=True)
    scratch=output/'scratch';scratch.mkdir();record_path=scratch/'records.json'
    child=None;observed=set();differences=[];pairs=set();counts=collections.Counter();started=time.monotonic()
    try:
        for number,(origin,keys) in enumerate(sorted(expected.items())):
            if child is None:
                child=subprocess.Popen([WORK/'api-general-macosx/general-harness'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
            query=dict(index[origin],dictionary=dictionary,origin=origin,scratch=str(scratch),output=str(record_path))
            child.stdin.write(json.dumps(query)+'\n');child.stdin.flush()
            response=child.stdout.readline()
            if not response:raise RuntimeError('Discovery harness exited before response')
            status=json.loads(response)
            if status.get('status')!='ok':
                differences.append(dict(archive=origin,status=status));break
            result=json.loads(record_path.read_text());records=result['records'];got={r['key'] for r in records}
            if not keys.issubset(got):differences.append(dict(archive=origin,missing=sorted(keys-got)))
            observed.update(got);counts['archives']+=1;counts['records']+=len(records)
            grouped=collections.defaultdict(lambda:collections.defaultdict(set))
            for r in records:
                if r['program']:grouped[r['program']][r['stage']].add(r['key'])
            for group in grouped.values():
                if len(group['vs'])==1 and len(group['ps'])==1:pairs.add((next(iter(group['vs'])),next(iter(group['ps']))))
            progress=dict(counts=counts,unique=len(observed),of=len(expected),differences=differences,seconds=time.monotonic()-started)
            (output/'progress.json').write_text(json.dumps(progress,indent=2)+'\n')
            record_path.unlink()
            if (number+1)%10==0:
                child.stdin.close();child.wait(timeout=10);child=None
        result=dict(counts=counts,unique=len(observed),referenceArchiveKeys=len(all_keys-main_keys),
            missingArchiveKeys=sorted((all_keys-main_keys)-observed),extraKeys=sorted(observed-all_keys),
            executableKeysPending=len(main_keys),programPairs=len(pairs),differences=differences,
            seconds=time.monotonic()-started,imageIndexSHA256=identity)
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        (output/'pairs.json').write_text(json.dumps(sorted(pairs))+'\n')
        (output/'keys.json').write_text(json.dumps(sorted(observed))+'\n')
        print(json.dumps({k:v for k,v in result.items() if k not in ('missingArchiveKeys','extraKeys','differences')},sort_keys=True))
        if differences or result['missingArchiveKeys'] or result['extraKeys']:raise SystemExit(1)
    finally:
        if child:
            child.stdin.close()
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        for path in scratch.iterdir():path.unlink()
        scratch.rmdir()

if __name__=='__main__':main()
