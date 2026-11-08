#!/usr/bin/env python3
"""Original archive fixtures and a real Mac Metal cache/dispatch check."""
from compression import zstd
import hashlib
import json
import os
from pathlib import Path
import struct
import runpy
import subprocess
import tempfile
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

# The general harness (build-api.py --general) and the results live in the caller's work folder.
WORK = DEPENDENCY_PATHS['shader_work']()

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'): raise RuntimeError('Run through guard.py')
    control = bytearray(0x880)
    for offset,value in [(0,0x98761234),(0x20,0x880),(0x700,32),(0x704,32),(0x748,1),(0x74c,1),(0x750,1)]:
        struct.pack_into('<I',control,offset,value)
    code = struct.pack('<4Q',0,0xe30000000007000f,0,0)
    key = hashlib.sha256(control+code).hexdigest()[:16]
    bnsh = bytearray(0xb20); bnsh[:4]=b'BNSH'; bnsh[0x60:0x64]=b'grsc'
    struct.pack_into('<IQ',bnsh,0x7c,1,0xa0); struct.pack_into('<Q',bnsh,0xb0,0x100)
    struct.pack_into('<Q',bnsh,0x130,0x180); struct.pack_into('<QQII',bnsh,0x188,0x200,0xb00,32,0x880)
    bnsh[0x200:0xa80]=control; bnsh[0xb00:]=code
    bahs = bytearray(0x1020); bahs[:4]=b'BAHS'; struct.pack_into('<II',bahs,0x18,32,0x1000)
    struct.pack_into('<II',bahs,0x38,0x28,1); struct.pack_into('<II',bahs,0x40,0x20,3)
    struct.pack_into('<IIIi',bahs,0x50,0,0,0x880,0x200-0x50)
    struct.pack_into('<6I',bahs,0x60,24,1,16,0,32,0)
    bahs[0x200:0xa80]=control; bahs[0x1000:]=code
    sarc = bytearray(0x60)+bnsh; sarc[:4]=b'SARC'; struct.pack_into('<HHII',sarc,4,0x14,0xfeff,len(sarc),0x60)
    sarc[0x14:0x18]=b'SFAT'; struct.pack_into('<HHI',sarc,0x18,12,1,0x65)
    struct.pack_into('<4I',sarc,0x20,0,0x01000000,0,len(bnsh)); sarc[0x30:0x34]=b'SFNT'
    sarc[0x38:0x42]=b'test.bnsh\0'
    fixtures = {'bnsh':bnsh,'bahs':bahs,'sarc':sarc,'zstd':zstd.compress(sarc)}
    child = subprocess.Popen([WORK/'api-general-macosx/general-harness'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    try:
        with tempfile.TemporaryDirectory(prefix='general-fixtures-',dir=WORK) as scratch:
            directory=Path(scratch); output=directory/'result.json'; counts={}
            def call(request):
                child.stdin.write(json.dumps(request)+'\n');child.stdin.flush()
                line=child.stdout.readline()
                if not line: raise RuntimeError('General harness exited before response')
                return json.loads(line)
            for name,data in fixtures.items():
                path=directory/(name+'.bin');path.write_bytes(data)
                request=dict(path=str(path),scratch=str(directory/'spill'),output=str(output),origin=name)
                response=call(request);assert response=={'status':'ok','records':1},(name,response)
                result=json.loads(output.read_text());assert result['records'][0]['key']==key
                assert result['records'][0]['stage']=='cs';counts[name]=1
                assert call(dict(request,cancelled=True))=={'status':'error','reason':'cancelled'}
            truncated=directory/'truncated.zst';truncated.write_bytes(fixtures['zstd'][:-1])
            assert call(dict(request,path=str(truncated)))['status']=='error';counts['truncation']=1
            bad=bytearray(bnsh);struct.pack_into('<Q',bad,0x80,2**64-1)
            malformed=directory/'bad.bnsh';malformed.write_bytes(bad)
            assert call(dict(request,path=str(malformed)))['status']=='error';counts['bounds']=1
            metal=call(dict(selftest=True,scratch=str(directory/'cache')))
            assert metal.get('status')=='ok',metal
            assert not list((directory/'spill').iterdir()),'Decoder leaked a spill file'
            result=dict(archives=counts,metal=metal,cancelled=4,spillFiles=0)
            (WORK/'general-result.json').write_text(json.dumps(result,indent=2)+'\n')
            print(json.dumps(result))
    finally:
        child.stdin.close()
        try: child.wait(timeout=10)
        except subprocess.TimeoutExpired: child.kill();child.wait()
    assert child.returncode==0

if __name__=='__main__':main()
