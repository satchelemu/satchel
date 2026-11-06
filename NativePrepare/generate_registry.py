#!/usr/bin/env python3
"""Compile the reviewed registry to immutable C data, with no runtime Python."""
import json, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
r = json.loads((root/'Tools/NativeRewriter/profiles/registry.json').read_text())
assert r['schema'] == 1
out = ['#include "Registry.h"', '#include <stdio.h>', '#include <string.h>']
serial = 0
def array(kind, rows):
    global serial
    if not rows: return 'NULL'
    serial += 1; name = 'r' + str(serial)
    out.append('static const ' + kind + ' ' + name + '[] = {' + ','.join(rows) + '};')
    return name
def string(s): return json.dumps(s)
def cuts(rows):
    result = []
    for at, old, new, why in rows:
        a,b=bytes.fromhex(old),bytes.fromhex(new); assert len(a)==len(b)
        x=array('uint8_t',list(map(str,a))); y=array('uint8_t',list(map(str,b)))
        result.append('{%d,%s,%s,%d,%s}'%(at,x,y,len(a),string(why)))
    return array('nr_cut',result)
def rules(rows):
    result=[]
    for rule in rows:
        fns=rule.get('functions',[rule]); fs=[]
        for fn in fns:
            fp=array('char *const',[string(x) for x in fn['fingerprints']]); cs=cuts(fn['cuts'])
            fs.append('{%d,%s,%d,%s,%d}'%(fn['size'],fp,len(fn['fingerprints']),cs,len(fn['cuts'])))
        fa=array('nr_match_function',fs)
        result.append('{%s,%s,%d}'%(string(rule['name']),fa,len(fs)))
    return array('nr_match_rule',result)
builds=[]
for bid,row in r['builds'].items():
    cs=cuts(row.get('CUTS',[])); obs=row.get('OBS',[]); caps=row.get('CAPABILITIES',[])
    oa=array('nr_observation',['{%d,%d,%d,%s}'%(a,n,int(w,16),string(why)) for a,n,w,why in obs])
    ca=array('char *const',[string(x) for x in caps]); recipe=row.get('FRAME_STEP',0)
    recipe=0xffffffff if recipe=='unsupported' else recipe
    builds.append((bid,'{%s,%d,%s,%d,%s,%d,%d,NULL,0}'%(cs,len(row.get('CUTS',[])),oa,len(obs),ca,len(caps),recipe),'CUTS' in row))
sdk=[(mod,rules(rows),len(rows)) for mod,rows in r['sdk_match_cuts'].items()]
out.append('void satchel_registry_profile(const uint8_t build[32], const char *module, nr_profile *p) {\nchar bid[33]; for(int i=0;i<16;i++) snprintf(bid+2*i,3,"%02x",build[i]);\nmemset(p,0,sizeof(*p)); int known_cuts=0;')
for bid,init,known in builds: out.append('if(!strcmp(bid,%s)){ *p=(nr_profile)%s; known_cuts=%d; }'%(string(bid),init,known))
out.append('if(strcmp(module,"main")) p->recipe=0;')
for mod,arr,n in sdk: out.append('if(!known_cuts && !strcmp(module,%s)){p->match_rules=%s;p->match_count=%d;}'%(string(mod),arr,n))
out.append('}')
# Snowdrop's page pools in MB by base title ID (snowdrop_pools); no row keeps upstream's default.
out.append('int satchel_registry_snowdrop(const char *title, uint32_t sizes[3]) {')
for title,sizes in r.get('snowdrop_pools',{}).items():
    assert len(sizes)==3 and all(isinstance(x,int) and x>0 for x in sizes) and sum(sizes)<4096
    out.append('if(!strcmp(title,%s)){sizes[0]=%d;sizes[1]=%d;sizes[2]=%d;return 1;}'%(string(title),*sizes))
out.append('return 0;\n}')
Path(sys.argv[1]).write_text('\n'.join(out)+'\n')
