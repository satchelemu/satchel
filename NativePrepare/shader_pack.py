#!/usr/bin/env python3
"""Create an external shader pack from a fully verified host package."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('package',type=Path); p.add_argument('output',type=Path); a=p.parse_args()
m=json.loads((a.package/'manifest.json').read_text())
assert m['titleID']=='0100F2C0115B6000' and m['version']=='1.4.3'
assert not a.output.exists(), 'Refusing to overwrite shader pack'
files={n:r for n,r in m['files'].items() if n=='profile.tsv' or n.startswith('msl/') or (n.startswith('dm/') and not any(n.endswith('.'+s) for s in ['fixups.tsv','gates.tsv']))}
a.output.mkdir(parents=True)
for top in ['msl']:
    subprocess.run(['cp','-cR',str(a.package/top),str(a.output/top)],check=True)
for n in files:
    if n.startswith('msl/'): continue
    dst=a.output/n; dst.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['cp','-c',str(a.package/n),str(dst)],check=True)
pack={k:m[k] for k in ['format','titleID','version','enginePin','forkPatchCount','shaderChain','shaderInputs','modules','environment']}
pack.update(contentVersion=720896,buildIDs={n:m['layout'][n]['layout']['BUILD_ID'] for n in m['modules']},files=files)
(a.output/'shader-pack.json').write_text(json.dumps(pack,indent=2)+'\n')
print('Shader pack created:',len(files),'bound files. Preparation verifies every copied digest.')
