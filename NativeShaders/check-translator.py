#!/usr/bin/env python3
"""Bounded native AOT versus pinned Mac translator parity, without Metal."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from oracle import DOTNET_ROOT, TOOLS, PACKAGES, jobs
ROOT = Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))

def native(library, code, job):
    payload = json.dumps(job).encode()
    output = ctypes.c_void_p()
    size = ctypes.c_int()
    status = library.satchel_shader_translate_v1(code, len(code), payload, len(payload), ctypes.byref(output), ctypes.byref(size))
    if status: return status, None
    try: result = ctypes.string_at(output, size.value)
    finally: library.satchel_shader_translate_free(output)
    version, ns, nr, ng = struct.unpack_from('<4I', result)
    if version != 1 or len(result) != 16 + ns + nr + ng: raise RuntimeError('Invalid native frame')
    return 0, (result[16:16+ns], result[16+ns:16+ns+nr], result[16+ns+nr:])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    work = ROOT / args.label
    work.mkdir(exist_ok=False)
    scratch = work / 'scratch'; scratch.mkdir()
    lib = ctypes.CDLL(str(ROOT / 'aot/repro/host-publish/SatchelShaderProbe.dylib'))
    lib.satchel_shader_translate_v1.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_int)]
    lib.satchel_shader_translate_free.argtypes = [ctypes.c_void_p]
    if lib.satchel_shader_runtime_features() != 0: raise RuntimeError('Dynamic code enabled')
    scope, rows = jobs()
    ordered = sorted(rows)
    if args.limit: ordered = ordered[:args.limit]
    start = time.monotonic()
    counts = {'attempted': 0, 'native_ok': 0, 'oracle_ok': 0, 'spirv_equal': 0, 'resource_bytes_equal': 0, 'resource_values_equal': 0}
    differences = []
    packs = {}
    try:
        for at in range(0, len(ordered), 32):
            batch = []
            for key in ordered[at:at+32]:
                name, index = rows[key]
                if name not in packs: packs[name] = scope['records']((PACKAGES/name).read_bytes())
                job, c1 = scope['job_for'](key, name, packs[name][index], str(scratch))
                batch.append(job)
            jobfile = scratch / 'jobs.json'; jobfile.write_text(json.dumps(batch))
            env = dict(os.environ, DOTNET_ROOT=str(DOTNET_ROOT), DOTNET_PROCESSOR_COUNT='2')
            # Capture and discard translator diagnostics, which can contain shader bytes.
            result = subprocess.run([TOOLS/'dmsh_xlate_bin/dmsh_xlate', jobfile, scratch], env=env, capture_output=True)
            for job in batch:
                key = job['key']; counts['attempted'] += 1
                code = Path(job['code']).read_bytes()
                status, translated = native(lib, code, job)
                counts['native_ok'] += status == 0
                spv, res = scratch/(key+'.spv'), scratch/(key+'.res.json')
                oracle_ok = spv.exists() and res.exists()
                counts['oracle_ok'] += oracle_ok
                equal = values = raw = False
                if translated and oracle_ok:
                    equal = translated[0] == spv.read_bytes()
                    raw = translated[1] == res.read_bytes()
                    values = json.loads(translated[1]) == json.loads(res.read_bytes())
                counts['spirv_equal'] += equal
                counts['resource_bytes_equal'] += raw
                counts['resource_values_equal'] += values
                if not (equal and raw): differences.append({'key': key, 'native_status': status, 'oracle_ok': oracle_ok, 'spirv_equal': equal, 'resource_bytes_equal': raw, 'resource_values_equal': values})
            for path in scratch.iterdir(): path.unlink()
            (work/'progress.json').write_text(json.dumps({'counts': counts, 'of': len(ordered), 'differences': len(differences), 'seconds': time.monotonic()-start}))
        summary = {'counts': counts, 'differences': differences, 'seconds': time.monotonic()-start}
        (work/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
        print(json.dumps({'counts': counts, 'differences': len(differences), 'seconds': summary['seconds']}))
        if differences: raise SystemExit(1)
    finally:
        for path in scratch.iterdir(): path.unlink()
        scratch.rmdir()

if __name__ == '__main__': main()
