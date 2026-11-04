#!/usr/bin/env python3
"""Two native callers must produce the same private outputs as serial calls."""
import concurrent.futures
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from oracle import PACKAGES,jobs
ROOT=Path.home()/'SatchelBuild/m5'
_spec=importlib.util.spec_from_file_location('translator_check',Path(__file__).with_name('check-translator.py'))
_module=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_module)

def main():
    work=ROOT/'concurrency';work.mkdir(exist_ok=False)
    lib=ctypes.CDLL(str(ROOT/'aot/repro/host-publish/SatchelShaderProbe.dylib'))
    lib.satchel_shader_translate_v1.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_int)]
    lib.satchel_shader_translate_free.argtypes=[ctypes.c_void_p]
    scope,rows=jobs();packs={};inputs=[]
    def run(item):
        status,result=_module.native(lib,*item)
        if status:raise RuntimeError('Native status '+str(status))
        return tuple(hashlib.sha256(data).hexdigest() for data in result)
    try:
        for key in sorted(rows)[::max(1,len(rows)//128)][:128]:
            name,index=rows[key]
            if name not in packs:packs[name]=scope['records']((PACKAGES/name).read_bytes())
            job,_=scope['job_for'](key,name,packs[name][index],str(work))
            code=Path(job['code']).read_bytes();Path(job['code']).unlink()
            inputs.append((code,job))
        expected=[run(item) for item in inputs]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:actual=list(pool.map(run,inputs))
        if actual!=expected:raise RuntimeError('Concurrent native output differs')
        print('128 serial versus two-caller NativeAOT translations match SPIR-V, resource JSON and reader bytes.')
    finally:
        for path in work.iterdir():path.unlink()
        work.rmdir()
if __name__=='__main__':main()
