#!/usr/bin/env python3
"""Independent byte comparisons. Never prints private payload bytes."""
import argparse, hashlib, json, struct, sys
from pathlib import Path
sys.dont_write_bytecode = True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'NativeHost'))
from package import image_sections, compare_sections
p=argparse.ArgumentParser(); p.add_argument('package',type=Path); p.add_argument('reference',type=Path); p.add_argument('--output',type=Path,required=True); p.add_argument('--container-only',action='store_true'); a=p.parse_args()
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(4*1024*1024),b''): h.update(b)
    return h.hexdigest()
results={}
for name in ([] if a.container_only else ['romfs.img']+[f'dm/{m}.{s}.tsv' for m in ['main','subsdk0','sdk'] for s in ['fixups','gates']]):
    results[name]=digest(a.package/name)==digest(a.reference/name)
    print(('PASS' if results[name] else 'FAIL')+': byte parity '+name)
# Packed ranges must match exactly, independent of the native manifest.
actual=image_sections(a.package/'game.dylib'); expected=image_sections(a.reference/'game.dylib')
with (a.package/'game.dylib').open('rb') as x,(a.reference/'game.dylib').open('rb') as y:
    for key,row in expected.items():
        other=actual.get(key); ok=other==row
        if ok:
            x.seek(row['offset']); y.seek(row['offset']); left=row['bytes']
            while left:
                n=min(left,4*1024*1024); ok &= x.read(n)==y.read(n); left-=n
        results['section '+','.join(key)]=ok
        print(('PASS' if ok else 'FAIL')+': section parity '+','.join(key))
# Also report the stronger requested whole-file condition, excluding only
# code-signature data and load-command fields changed by codesign.
def normalized(path):
    b=bytearray(path.read_bytes()); n=struct.unpack_from('<I',b,16)[0]; at=32; commands=[]
    for _ in range(n):
        cmd,size=struct.unpack_from('<II',b,at); d=bytearray(b[at:at+size])
        if cmd==0x1d:
            off,length=struct.unpack_from('<II',d,8); b=b[:off]
        else:
            if cmd==0x19 and d[8:24].rstrip(b'\0')==b'__LINKEDIT': d[32:40]=bytes(8);d[48:56]=bytes(8)
            commands.append(d)
        at+=size
    c=b''.join(commands);struct.pack_into('<II',b,16,len(commands),len(c));b[32:at]=c+bytes(at-32-len(c))
    return b
x,y=normalized(a.package/'game.dylib'),normalized(a.reference/'game.dylib')
results['whole dylib excluding signature']=x==y
if x!=y:
    diffs=[i for i,(u,v) in enumerate(zip(x,y)) if u!=v]
    print('FAIL: whole dylib parity; different byte count',len(diffs),'first offsets',diffs[:24], 'lengths',len(x),len(y))
else: print('PASS: whole dylib parity excluding code signature')
a.output.write_text(json.dumps(results,indent=2)+'\n')
if not all(results.values()): sys.exit(1)
