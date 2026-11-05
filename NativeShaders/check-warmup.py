#!/usr/bin/env python3
"""Stratified Zelda timing with bounded records and incremental metrics, no source logs."""
import collections
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
from oracle import jobs, PROFILE, PACKAGES

WORK=Path.home()/'SatchelBuild/m5'
CORPUS=Path.home()/'SatchelBuild/shaders/package/TOTK-1.4.3-src/msl/b-26dc2fa9477c'

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):raise RuntimeError('Run through guard.py')
    scope,rows=jobs();chosen=[]
    for stage in ['vs','ps','cs']:
        keys=sorted(k for k in rows if rows[k][0].startswith(stage+'_'))
        chosen.extend(keys[i*(len(keys)-1)//63] for i in range(64))
    pairs=collections.defaultdict(set)
    for path in [PROFILE/'shader_pairs.tsv',PACKAGES.parent/'pso_pairs.tsv']:
        for line in path.read_text().splitlines():
            if line.strip() and not line.startswith('#'):
                v,p=line.split();pairs[v].add(p)
    def flags(name):
        path=PROFILE/name
        return {s.split()[0] for s in path.read_text().splitlines() if s.strip() and not s.startswith('#')} if path.exists() else set()
    mapping,dual=flags('bf_want.tsv'),flags('dual_want.tsv')
    results=collections.defaultdict(list);summaries={};expected_sources=0
    with tempfile.TemporaryDirectory(prefix='warmup-timing-',dir=WORK) as temp:
        directory=Path(temp);manifest=[];last_name=None;records=None
        for key in sorted(chosen,key=lambda k:rows[k]):
            name,index=rows[key]
            if name!=last_name:records=scope['records']((PACKAGES/name).read_bytes());last_name=name
            path=directory/(key+'.bin');path.write_bytes(records[index]);wanted=set()
            for p in pairs[key]:wanted.update(map(int,re.findall(r'\[\[user\(locn(\d+)\)',(CORPUS/(p+'_b.metal')).read_text())))
            row=dict(key=key,input=str(path),stage=name[:2],wanted=sorted(wanted),mappingOnly=key in mapping,dual=key in dual)
            if key in scope['FACTS']:row['facts']=scope['FACTS'][key]
            manifest.append(row);expected_sources+=len(list(CORPUS.glob(key+'_*.metal')))
        records=None;manifest_path=directory/'manifest.json';manifest_path.write_text(json.dumps(manifest))
        with (WORK/'warmup-timings.jsonl').open('w') as output:
            for mode in ['initial','restart']:
                child=subprocess.Popen([WORK/'warmup-harness',manifest_path,directory/'cache',mode],stdout=subprocess.PIPE,text=True)
                try:
                    for line in child.stdout:
                        entry=json.loads(line);output.write(line);output.flush()
                        results[entry['mode']].extend(entry['records']);summaries[entry['mode']]=entry['progress']
                        (WORK/'warmup-progress.json').write_text(json.dumps(summaries,indent=2)+'\n')
                    assert child.wait()==0,'Warm-up harness failed'
                finally:
                    if child.poll() is None:child.terminate();child.wait(timeout=10)
    def distribution(values):
        v=sorted(values)
        return dict(count=len(v),total=sum(v),median=statistics.median(v),p95=v[int(.95*(len(v)-1))],maximum=max(v))
    timings={}
    for mode,records in results.items():
        assert len(records)==192 and len({r['requestSHA256'] for r in records})==192
        assert sum(len(r['compileSeconds']) for r in records)==expected_sources
        timings[mode]=dict(translation=distribution([r['translationSeconds'] for r in records]),
            compilation=distribution([v for r in records for v in r['compileSeconds'].values()]),
            lookup=distribution([r['lookupSeconds'] for r in records]),persistence=distribution([r['persistenceSeconds'] for r in records]))
    assert summaries['emptyApplicationCache']['sourceCacheHits']==0
    assert summaries['liveLibraries']['sourceCacheHits']==192 and summaries['liveLibraries']['compiledLibraries']==0
    assert summaries['restartedProcess']['sourceCacheHits']==192 and summaries['restartedProcess']['compiledLibraries']>0
    result=dict(title='Zelda 1.4.3',records=192,stages={'vs':64,'ps':64,'cs':64},sources=expected_sources,
        summaries=summaries,timings=timings,platform='Mac native iOS-equivalent path, not phone timing',
        compilerCache='OS Metal cache pre-existing and uncontrolled; empty refers only to private application source cache',liveLibraryLimit=512)
    (WORK/'warmup-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
