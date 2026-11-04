#!/usr/bin/env python3
"""Development-only Mac signer for a native phone-produced package."""
import argparse, hashlib, json, os, re, runpy, subprocess, uuid
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('package',type=Path); p.add_argument('--identity',required=True); a=p.parse_args()
manifest=a.package/'manifest.json'; m=json.loads(manifest.read_text())
assert m['signingStatus']=='needs signature', 'Expected unsigned development package'
dylib=a.package/'game.dylib'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()
assert dylib.stat().st_size==m['files']['game.dylib']['bytes'] and digest(dylib)==m['files']['game.dylib']['sha256']
temporary=a.package/('.sign-'+uuid.uuid4().hex+'.dylib')
try:
    subprocess.run(['cp','-c',str(dylib),str(temporary)],check=True)
    subprocess.run(['codesign','--force','--sign',a.identity,'--identifier','com.dell.satchel.dev.game','--timestamp=none',str(temporary)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(temporary)],check=True)
    metadata=subprocess.run(['codesign','-dvv',str(temporary)],capture_output=True,text=True,check=True).stderr
    team=re.search(r'TeamIdentifier=(\w+)',metadata).group(1)
    expected=runpy.run_path(str(Path(__file__).resolve().parents[1]/'NativeApp/build_support.py'))['signing_team'](Path(__file__).resolve().parents[1],required=False)
    assert expected is None or team==expected, 'Unexpected signing team '+team
    m['files']['game.dylib']={'bytes':temporary.stat().st_size,'sha256':digest(temporary)}
    m['signingStatus']='signed'
    os.replace(temporary,dylib)
    pending=a.package/('.manifest-'+uuid.uuid4().hex)
    pending.write_text(json.dumps(m,indent=2)+'\n'); os.replace(pending,manifest)
finally:
    temporary.unlink(missing_ok=True)
print('PASS: Mac-signed native container; manifest signature state and digest updated.')
