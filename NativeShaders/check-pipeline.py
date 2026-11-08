#!/usr/bin/env python3
"""Native output versus the pinned engine's Mac MSL and reflection for one game, streamed to scratch.

The game is the oracle's profile and packages (SATCHEL_SHADER_PROFILE, SATCHEL_SHADER_PACKAGES; default Zelda);
--corpus is the Mac pipeline's b-<chain> folder for it and --keys limits the run to a key list."""
import argparse
import collections
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import statistics
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from oracle import ENGINE, TOOLS, PROFILE, PACKAGES, jobs, functions
import importlib.util
from guard import atomic_json, resources
_spec=importlib.util.spec_from_file_location("check_translator",Path(__file__).with_name("check-translator.py"))
_translator=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_translator)
native=_translator.native
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))
# The NativeAOT library is read from the shader products. The harnesses (build-translation.py) and each
# labelled run live in the caller's work folder; the dependency rebuild points SATCHEL_SHADER_WORK at <root>/m5
# so its pipeline-full proof stays where NativeHost/shader_dependency.py reads it.
PRODUCTS = DEPENDENCY_PATHS['shader_deps']()
WORK = DEPENDENCY_PATHS['shader_work']()
DEFAULT_CORPUS = Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'shaders/package/TOTK-1.4.3-src/msl/b-26dc2fa9477c'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--start',type=int,default=0)
    parser.add_argument('--stop-on-difference',action='store_true')
    parser.add_argument('--key')
    parser.add_argument('--source-stages',action='store_true')
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--label', required=True)
    parser.add_argument('--corpus', type=Path, default=DEFAULT_CORPUS, help='The Mac pipeline output folder b-<chain> of this game')
    parser.add_argument('--keys', type=Path, help='Only these keys (one per line); keys the Mac pipeline did not build are counted, not compared')
    parser.add_argument('--batch-size', type=int, default=500)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--max-batches', type=int, default=0, help='Stop at a resumable batch boundary')
    args = parser.parse_args()
    CORPUS = args.corpus
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):
        parser.error('Run through NativeShaders/guard.py --light for RSS, swap and disk monitoring')
    if not 1 <= args.batch_size <= 2000: parser.error('Batch size must be between 1 and 2000')
    if args.max_batches<0: parser.error('Maximum batch count cannot be negative')
    if Path(args.label).name != args.label or args.label in ('.','..'): parser.error('Label must be one directory name')
    work = WORK / args.label; work.mkdir(parents=True, exist_ok=args.resume)
    scratch = work / 'scratch'; scratch.mkdir(exist_ok=args.resume)
    for path in scratch.iterdir(): path.unlink()
    executable=WORK/('source-harness' if args.source_stages else 'translation-harness')
    child=None
    scope, rows = jobs()
    sys.path.insert(0,str(TOOLS))
    import c1_guard, cb_extent
    if args.source_stages:
        lib=ctypes.CDLL(str(PRODUCTS/'aot/repro/host-publish/SatchelShaderProbe.dylib'))
        lib.satchel_shader_translate_v1.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_int)]
        lib.satchel_shader_translate_free.argtypes=[ctypes.c_void_p]
        spv_scope=functions({'spv_single_lane','_ep_drop','int_inputs'})
        spv_scope.update(SG_OPS={333:'true',334:'pred',335:'pred',336:'true',337:'value',338:'value',339:'ballot',345:'value',346:'value',347:'value',348:'value'},
            SG_BUILTINS={36:'one',41:'zero',4416:'one4',4417:'one4',4418:'zero4',4419:'one4',4420:'zero4'},_CAST=('as_type<int>(', 'as_type<uint>('))
        sys.path.insert(0,str(TOOLS))
        import dmsh_atomic, c1_guard, cb_extent

    pc = {}
    for line in (PROFILE/'shader_map.tsv').read_text().splitlines():
        cols = line.split('\t')
        if len(cols)>4 and cols[0].endswith('.bin'): pc.setdefault(cols[3], cols[4])
    for line in (PROFILE/'shader_map.tsv.conflicts.tsv').read_text().splitlines():
        cols = line.split('\t')
        if len(cols)>1 and not line.startswith(('#','dm_')) and cols[0] in pc: pc[cols[0]]=cols[1]
    pairs = collections.defaultdict(set)
    for table in ['shader_pairs.tsv','pso_pairs.tsv']:
        path=PROFILE/table
        if table=='pso_pairs.tsv' and not path.exists(): path=PACKAGES.parent/table
        if not path.exists(): continue
        for line in path.read_text().splitlines():
            if line.strip() and not line.startswith('#'):
                v,p=line.split();pairs[v].add(p)
    def wanted(table):
        path=PROFILE/table
        return {line.split()[0] for line in path.read_text().splitlines() if line.strip() and not line.startswith('#')} if path.exists() else set()
    bf, dual = wanted('bf_want.tsv'), wanted('dual_want.tsv')
    locations={}
    def ps_locations(key):
        if key not in locations:
            path=CORPUS/(key+'_b.metal')
            locations[key]={int(x) for x in re.findall(r'\[\[user\(locn(\d+)\)',path.read_text())} if path.exists() else set()
        return locations[key]
    inventory=collections.defaultdict(set)
    for path in CORPUS.glob('*.metal'):
        key,suffix=path.stem.rsplit('_',1);inventory[key].add(suffix)
    # The engine's own build keeps only <key>_b.metal and compiles its variants from it with these pure text
    # functions; a variant exists when its Mac library does. A retained corpus with variant sources is read as is.
    sys.path.insert(0,str(TOOLS))
    import dmsh_vin, dmsh_pull, dmsh_side, dmsh_fetch
    for path in CORPUS.glob('*_b.metallib'):
        key=path.name[:-len('_b.metallib')]
        inventory[key].update(sfx for sfx in ('bv','bp','bx','bf') if (CORPUS/(key+'_'+sfx+'.metallib')).exists())
    def expected_source(key,suffix):
        path=CORPUS/(key+'_'+suffix+'.metal')
        if path.exists(): return path.read_bytes()
        full=(CORPUS/(key+'_b.metal')).read_text()
        if suffix=='bv': text=dmsh_vin.variant(full)[0]
        elif suffix=='bp': text=dmsh_pull.variant(full)[0]
        elif suffix=='bx': text=dmsh_side.unit(full)[0]
        else: text=dmsh_fetch.variant(full,json.loads((CORPUS/(key+'_b.json')).read_bytes())['bindings'],om_only_ok=key in bf)[0]
        return (text or '').encode()
    counts=collections.Counter(); differences=[]; packs={}; timings=[]; start=time.monotonic()
    ordered=[args.key] if args.key else sorted(rows)[args.start:]
    if args.keys:
        listed=set(args.keys.read_text().split())
        ordered=[k for k in ordered if k in listed]
    unbuilt=[k for k in ordered if not (CORPUS/(k+'_b.json')).exists()]
    ordered=[k for k in ordered if k not in set(unbuilt)]
    if args.limit: ordered=ordered[:args.limit]
    digest=hashlib.sha256()
    for path in [Path(__file__),executable,PRODUCTS/'aot/repro/host-publish/SatchelShaderProbe.dylib']:
        digest.update(path.read_bytes())
    facts_identity={'rows':rows,'facts':scope['FACTS'],'pairing':pc,
        'pairs':{k:sorted(v) for k,v in pairs.items()},'mapping':sorted(bf),'dual':sorted(dual)}
    input_digest=hashlib.sha256(json.dumps(facts_identity,sort_keys=True,separators=(',',':')).encode())
    manifest=CORPUS.parents[1]/'manifest.json'
    input_digest.update((manifest if manifest.exists() else CORPUS/'_b_build.json').read_bytes())
    config={'version':1,'source_stages':args.source_stages,'batch_size':args.batch_size,
            'keys_sha256':hashlib.sha256('\n'.join(ordered).encode()).hexdigest(),
            'count':len(ordered),'implementation_sha256':digest.hexdigest(),
            'input_metadata_sha256':input_digest.hexdigest()}
    checkpoint=work/'checkpoint.json'
    next_index=0;elapsed=0;completed_batches=0;session_batches=0
    if args.resume:
        saved=json.loads(checkpoint.read_text())
        if saved['configuration']!=config: raise RuntimeError('Resume configuration or executable changed; use a fresh label')
        if not 0<=saved['next_index']<=len(ordered) or saved['counts'].get('attempted',0)!=saved['next_index']:
            raise RuntimeError('Invalid resume checkpoint position')
        next_index=saved['next_index'];counts.update(saved['counts']);differences=saved['differences']
        timings=saved['timings'];elapsed=saved['seconds'];completed_batches=saved['completed_batches']
    def save():
        value={'configuration':config,'next_index':next_index,'counts':counts,'differences':differences,
               'timings':timings,'seconds':elapsed+time.monotonic()-start,'completed_batches':completed_batches}
        atomic_json(checkpoint,value)
        atomic_json(work/'progress.json',{'counts':counts,'of':len(ordered),'next_index':next_index,
            'differences':len(differences),'seconds':value['seconds'],'completed_batches':completed_batches})
        atomic_json(work/'differences.json',differences)
    save()
    def stop_batch():
        nonlocal child
        if child is not None:
            try: child.stdin.close()
            except BrokenPipeError: pass
            try: child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill();child.wait()
            child=None
    def reply(request):
        child.stdin.write(json.dumps(request)+'\n');child.stdin.flush()
        line=child.stdout.readline()
        if not line: raise RuntimeError('Shader harness exited before response, exit '+str(child.poll()))
        try: return json.loads(line)
        except json.JSONDecodeError: raise RuntimeError('Shader harness returned invalid JSON, payload withheld') from None
    try:
        for ordered_index in range(next_index,len(ordered)):
            key=ordered[ordered_index]
            if child is None:
                _,_,_,reason=resources()
                if reason: raise RuntimeError(reason)
                child=subprocess.Popen([executable],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
                batch_count=0
            name,index=rows[key]
            if name not in packs: packs[name]=scope['records']((PACKAGES/name).read_bytes())
            job,c1=scope['job_for'](key,name,packs[name][index],str(scratch))
            job.update(source=f'{name}#{index}',pc_paired=pc[key])
            want=set().union(*(ps_locations(p) for p in pairs[key])) if pairs[key] else set()
            request={'input':job['code'],'output':str(scratch),'job':job,'wanted':sorted(want),'mappingOnly':key in bf,'dual':key in dual}
            if not args.source_stages:
                record_path=scratch/'record.bin';record_path.write_bytes(packs[name][index])
                request['input']=str(record_path);request['record']=True
                if key in scope['FACTS']:request['facts']=scope['FACTS'][key]
            if args.source_stages:
                code=Path(job['code']).read_bytes()
                spv_path=scratch/'stage.spv'; raw_path=scratch/'raw.txt'
                def raw():
                    status,result=native(lib,code,job)
                    if status: raise RuntimeError('Native translator status '+str(status))
                    spv_path.write_bytes(result[0])
                    if job['stage']=='vs': spv_scope['spv_single_lane'](str(spv_path))
                    options=dmsh_atomic.cross_args(['--msl','--msl-version','20300','--msl-argument-buffers','--msl-texture-buffer-native','--msl-pad-fragment-output'],str(spv_path))
                    cross=subprocess.run([TOOLS/'spirv_cross_bin/spirv-cross',*options,spv_path,'--output',raw_path],capture_output=True)
                    if cross.returncode: raise RuntimeError('Pinned SPIRV-Cross failed, diagnostics withheld')
                    return json.loads(result[1])
                request['resources']=raw();request['input']=str(raw_path)
                if job['stage']=='vs':
                    probe=dict(request,integerInputsOnly=True)
                    actual_attrs=reply(probe)
                    expected_attrs=spv_scope['int_inputs'](raw_path.read_text())
                    if actual_attrs!=expected_attrs: differences.append({'key':key,'integer_input_detection':True})
                    extra={k:v for k,v in actual_attrs.items() if k not in job['attr']}
                    if extra: job['attr'].update(extra);request['resources']=raw()
            response=reply(request);counts['attempted']+=1
            if 'preparation_seconds' in response: timings.append(response['preparation_seconds'])
            if response['status'] != 'ok': differences.append({'key':key,**response})
            else:
                counts['translated']+=1
                expected_names=inventory[key]
                actual_names={p.stem for p in scratch.glob('*.metal')}
                if actual_names != expected_names:
                    differences.append({'key':key,'variant_inventory':{'missing':sorted(expected_names-actual_names),'extra':sorted(actual_names-expected_names)}})
                for suffix in sorted(expected_names & actual_names):
                    expected=expected_source(key,suffix);actual=(scratch/(suffix+'.metal')).read_bytes()
                    counts[suffix+'_attempted']+=1;counts[suffix+'_byte_equal']+=actual==expected
                    if actual!=expected:
                        differences.append({'key':key,'kind':suffix,'expected_sha256':hashlib.sha256(expected).hexdigest(),'actual_sha256':hashlib.sha256(actual).hexdigest(),'whitespace_only':re.sub(rb'\s+',b'',actual)==re.sub(rb'\s+',b'',expected),'trailing_only':actual.rstrip()==expected.rstrip(),'expected_bytes':len(expected),'actual_bytes':len(actual)})
                expected=json.loads((CORPUS/(key+'_b.json')).read_bytes())
                # Offline compiler identity has no truthful on-phone equivalent.
                for field in ['chain_metal','chain_compiler']: expected.pop(field,None)
                actual=json.loads((scratch/'reflection.json').read_bytes())
                base=(scratch/'b.metal').read_text();bindings=actual['bindings']
                definitions=c1_guard.definitions_of(base);span=(len(c1)+255)//256*256+256
                expected_c1=any(not c1_guard.proven(i,definitions,span) for i in set(c1_guard.ACCESS.findall(base)))
                expected_extents={str(k):v for k,v in cb_extent.extents(base,bindings).items()}
                expected_unwritten=cb_extent.unwritten(base,bindings)
                for field,expected_value in [('c1_unproven',expected_c1),('extents',expected_extents),('unwritten',expected_unwritten)]:
                    equal=response.get(field)==expected_value;counts[field+'_equal']+=equal
                    if not equal: differences.append({'key':key,'analysis':field})
                counts['reflection_values_equal']+=actual==expected
                canonical=json.dumps(expected,ensure_ascii=False,indent=1).encode()
                counts['reflection_bytes_equal']+=(scratch/'reflection.json').read_bytes()==canonical
                if actual!=expected: differences.append({'key':key,'reflection_fields':sorted(k for k in set(actual)|set(expected) if actual.get(k)!=expected.get(k))})
                elif (scratch/'reflection.json').read_bytes()!=canonical: differences.append({'key':key,'reflection_serialization':True})
            for path in scratch.iterdir(): path.unlink()
            next_index=ordered_index+1;batch_count+=1
            if batch_count>=args.batch_size:
                stop_batch();completed_batches+=1;session_batches+=1;save()
                if args.max_batches and session_batches>=args.max_batches: break
            elif counts['attempted']%100==0: save()
            if differences and args.stop_on_difference: break
        if child is not None:
            stop_batch();completed_batches+=1
        save()
        summary={'counts':counts,'differences':differences,'not_built_by_reference':unbuilt,'seconds':elapsed+time.monotonic()-start,
                 'complete':next_index==len(ordered),'completed_batches':completed_batches,
                 'excluded_reflection_fields':['chain_metal','chain_compiler']}
        if timings:
            summary['native_preparation_seconds']={'count':len(timings),'total':sum(timings),
                'median':statistics.median(timings),'p95':sorted(timings)[int((len(timings)-1)*.95)],'maximum':max(timings)}
        (work/'result.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps({'counts':counts,'differences':len(differences),'seconds':summary['seconds']}))
        if differences: raise SystemExit(1)
    finally:
        stop_batch()
        for path in scratch.iterdir():path.unlink()
        scratch.rmdir()

if __name__=='__main__': main()
