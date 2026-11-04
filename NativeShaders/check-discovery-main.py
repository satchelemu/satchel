#!/usr/bin/env python3
"""Complete real discovery with the executable image already read by Prepare."""
import collections
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import types

WORK=Path.home()/'SatchelBuild/m5'
ENGINE=Path.home()/'SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/NX2Apple'
PROFILE=Path.home()/'SatchelBuild/host/engine/dm/games/0100F2C0115B6000'

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):raise RuntimeError('Run through guard.py')
    sys.path.insert(0,str(ENGINE/'dm/tools'))
    # Host adapter only. Keep the pinned NSO parser's length and SHA-256 checks;
    # use the installed public Compression buffer API instead of a new package.
    compression=ctypes.CDLL('/usr/lib/libcompression.dylib')
    decode=compression.compression_decode_buffer
    decode.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_int]
    decode.restype=ctypes.c_size_t
    def decompress(raw,uncompressed_size):
        if not 0<=uncompressed_size<=512*1024**2:raise ValueError('NSO segment exceeds adapter limit')
        output=ctypes.create_string_buffer(uncompressed_size+1)
        count=decode(output,len(output),raw,len(raw),None,0x101)
        if count!=uncompressed_size:raise ValueError('NSO LZ4 output length mismatch')
        return output.raw[:count]
    block=types.ModuleType('lz4.block');block.decompress=decompress;block.LZ4BlockError=ValueError
    package=types.ModuleType('lz4');package.block=block
    sys.modules.update({'lz4':package,'lz4.block':block})
    import nso
    source=Path.home()/'SatchelBuild/zelda/work/extract/v720896/ExeFS/main'
    output=WORK/'discovery-main-2';output.mkdir();started=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='image-',dir=output) as temporary:
        root=Path(temporary);image=root/'image.bin'
        module=nso.load(source.read_bytes(),'private main')
        image.write_bytes(module.image);del module
        query=dict(path=str(image),origin='main',scratch=str(root/'scratch'),output=str(output/'records.json'))
        child=subprocess.run([WORK/'api-general-macosx/general-harness'],input=json.dumps(query)+'\n',text=True,capture_output=True)
        if child.returncode:raise RuntimeError('Executable discovery failed; private diagnostics withheld')
        response=json.loads(child.stdout)
        if response.get('status')!='ok':raise RuntimeError('Executable discovery status '+response.get('reason','unknown'))
    records=json.loads((output/'records.json').read_text())['records'];observed={r['key'] for r in records}
    expected=set();archive=set()
    for line in (PROFILE/'shader_map.tsv').read_text().splitlines():
        columns=line.split('\t')
        if len(columns)>4 and columns[0].endswith('.bin'):
            (expected if columns[4].startswith('main') else archive).add(columns[3])
    grouped=collections.defaultdict(lambda:collections.defaultdict(set))
    for r in records:
        if r['program']:grouped[r['program']][r['stage']].add(r['key'])
    main_pairs={(next(iter(g['vs'])),next(iter(g['ps']))) for g in grouped.values() if len(g['vs'])==1 and len(g['ps'])==1}
    archive_pairs={tuple(p) for p in json.loads((WORK/'discovery-full/pairs.json').read_text())}
    reference_pairs=set()
    for table in [PROFILE/'shader_pairs.tsv',Path.home()/'SatchelBuild/zelda/work/shader/pso_pairs.tsv']:
        reference_pairs.update(tuple(l.split()) for l in table.read_text().splitlines() if l.strip() and not l.startswith('#'))
    result=dict(mainRecords=len(records),mainUnique=len(observed),missingMainKeys=sorted(expected-observed),
        totalUnique=len(archive|observed),referenceUnique=len(archive|expected),extraKeys=sorted(observed-archive-expected),
        totalPairs=len(archive_pairs|main_pairs),referencePairs=len(reference_pairs),
        missingPairs=sorted(reference_pairs-archive_pairs-main_pairs),extraPairs=sorted((archive_pairs|main_pairs)-reference_pairs),
        seconds=time.monotonic()-started)
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    if any(result[k] for k in ['missingMainKeys','extraKeys','missingPairs','extraPairs']):raise SystemExit(1)

if __name__=='__main__':main()
