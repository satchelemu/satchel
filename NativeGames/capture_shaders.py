#!/usr/bin/env python3
"""Normalize NVN runtime captures into NX2Apple's existing offline shader inputs."""
from pathlib import Path
import argparse,collections,hashlib,importlib.util,json,os,re,struct,sys
sys.dont_write_bytecode=True

def normalize(capture,output,engine,title,profiles=None,require_pairs=False):
    capture,output,engine=map(lambda p:Path(p).resolve(),[capture,output,engine])
    repo=Path(__file__).resolve().parents[1]
    if output.is_relative_to(repo):raise ValueError('Private shader output must be outside the repository')
    if output.exists():raise ValueError('Output already exists; use a new capture iteration directory')
    if not re.fullmatch(r'[0-9A-Fa-f]{16}',title):raise ValueError('Expected a 16-digit title ID')
    os.environ['DM_GAME']=title.upper()
    if profiles:os.environ.update(DM_GAMES_DIR=str(Path(profiles).resolve()),DM_GAMES_FIRST='1')
    sys.path[:0]=[str(engine/'dm/gfx/tools'),str(engine/'dm/tools'),str(engine/'dm/games')]
    spec=importlib.util.spec_from_file_location('runtime_shader_split',engine/'dm/gfx/tools/shader_split.py')
    split=importlib.util.module_from_spec(spec);spec.loader.exec_module(split)
    shaders={};total=0
    controls=sorted(capture.glob('*.ctl'))
    if not controls:raise ValueError('Capture has no control sections')
    if len(controls)>100000:raise ValueError('Capture exceeds 100,000 shaders')
    for path in controls:
        key=path.stem
        if not re.fullmatch(r'[0-9a-f]{16}',key):raise ValueError('Invalid capture key filename')
        codepath=path.with_suffix('.code')
        if path.is_symlink() or codepath.is_symlink():raise ValueError('Capture symlinks are not accepted')
        if not 0x760<=path.stat().st_size<=0x880:raise ValueError(f'{key}: invalid control length')
        if not codepath.exists() or not 0<codepath.stat().st_size<1<<24:raise ValueError(f'{key}: missing or oversized code area')
        ctl=path.read_bytes();code=codepath.read_bytes();total+=len(ctl)+len(code)
        if total>4*1024**3:raise ValueError('Capture exceeds 4 GiB')
        if len(ctl)<0x24 or len(ctl)<split.ctl_len(ctl):raise ValueError(f'{key}: truncated control section')
        ctl=ctl[:split.ctl_len(ctl)]
        stage=split.glslc_stage(ctl,code)
        if not stage:raise ValueError(f'{key}: unsupported shader stage header')
        problem=split.check('runtime capture',stage,ctl,code)
        if problem:raise ValueError(f'{key}: {problem}')
        if split.skey(ctl,code)!=key:raise ValueError(f'{key}: capture digest mismatch')
        shaders[key]=(stage,ctl,code)
    extra={p.stem for p in capture.glob('*.code')}-shaders.keys()
    if extra:raise ValueError('Code area without a matching control section')
    pairs=set();pairfile=capture/'draw-pairs.tsv'
    if require_pairs and not pairfile.exists():raise ValueError('Draw-pair evidence is missing; use the runtime capture host patch')
    if pairfile.exists():
        for line in pairfile.read_text().splitlines():
            if not line or line.startswith('#'):continue
            fields=line.split('\t')
            if len(fields)!=2:raise ValueError('Invalid captured draw pair')
            v,p=fields
            if v not in shaders or p not in shaders:raise ValueError('Draw pair refers to an uncaptured shader')
            if shaders[v][0]!='vs' or shaders[p][0]!='ps':raise ValueError('Captured draw pair stages do not match')
            pairs.add((v,p))
    # Match shader_split.py record layout, key rules and compute facts exactly.
    records=collections.defaultdict(list);rows=[];facts={};stages=collections.Counter()
    for key,(stage,ctl,code) in sorted(shaders.items()):
        name=f'{stage}_runtime.bin'
        record=bytearray(b'BTCDBTCD'+bytes(0x30));struct.pack_into('<II',record,0x28,len(ctl),len(code));record+=code+ctl
        if record.find(split.CTL_MAGIC)!=0x38+len(code) or record.count(b'BTCDBTCD')!=1:raise ValueError(f'{key}: ambiguous package record separators')
        rows.append((name,str(len(records[name])),split.STAGE_NAME[stage],key,'/runtime/'+key))
        records[name].append(record);stages[stage]+=1
        if stage=='cs':
            x,y,z,shared=struct.unpack_from('<4I',ctl,0x748)
            if not x*y*z:raise ValueError(f'{key}: zero compute threadgroup dimension')
            facts[key]={'local':[x,y,z],'shared':(shared+15)//16*16}
    manual=split.GAME.table('shader_facts_manual.tsv')
    if manual.exists():
        for line in manual.read_text().splitlines():
            if not line or line.startswith('#'):continue
            key,value=line.split('\t',1)
            if key in shaders:facts[key]={**facts.get(key,{}),**json.loads(value)}
    (output/'pkg').mkdir(parents=True)
    (output/'tables').mkdir()
    for name,parts in records.items():(output/'pkg'/name).write_bytes(b''.join(parts))
    tables=output/'tables'
    (tables/'shader_map.tsv').write_text('# Runtime NVN capture, observed shader coverage only.\npackage\tindex\tstage\tdm_ctl_sha16\tsource\n'+''.join('\t'.join(r)+'\n' for r in rows))
    (tables/'shader_facts.tsv').write_text('# Compute facts from captured control sections; other queries use the pinned translator defaults.\n'+''.join(k+'\t'+json.dumps(v,sort_keys=True,separators=(',',':'))+'\n' for k,v in sorted(facts.items())))
    (tables/'shader_pairs.tsv').write_text('# Vertex and pixel keys observed together at a draw, before shader-library lookup.\n'+''.join(v+'\t'+p+'\n' for v,p in sorted(pairs)))
    (tables/'pso_pairs.tsv').write_text('# No inferred pairs. All observed pairs are in shader_pairs.tsv.\n')
    (tables/'pso_manifest.tsv').write_text('# Empty seed until translated pipelines are recorded by the host.\n')
    summary=dict(format=1,titleID=title.upper(),source='NVN runtime capture',coverage='observed programs only, not whole-game coverage',shaders=len(shaders),stages=dict(stages),drawPairs=len(pairs),drawPairEvidence=pairfile.exists(),computeFacts=sum(s[0]=='cs' for s in shaders.values()),inputBytes=total)
    (output/'capture-index.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--engine',type=Path,required=True);p.add_argument('--title',required=True)
    p.add_argument('--profiles',type=Path);p.add_argument('--require-pairs',action='store_true')
    a=p.parse_args()
    try:print(json.dumps(normalize(a.capture,a.output,a.engine,a.title,a.profiles,a.require_pairs)))
    except (ValueError,OSError) as e:p.exit(1,'Capture normalization failed: '+str(e)+'\n')

if __name__=='__main__':main()
