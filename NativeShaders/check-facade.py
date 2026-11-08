#!/usr/bin/env python3
"""Exercise the public NativeAOT ABI with original synthetic data and bad framing."""
import ctypes
import json
from pathlib import Path
import runpy
import struct
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

# Reads the NativeAOT library from the shader products; writes nothing.
ROOT = DEPENDENCY_PATHS['shader_deps']()

def main():
    lib = ctypes.CDLL(str(ROOT/'aot/repro/host-publish/SatchelShaderProbe.dylib'))
    call = lib.satchel_shader_translate_v1
    call.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                     ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_int)]
    lib.satchel_shader_translate_free.argtypes = [ctypes.c_void_p]
    code = struct.pack('<4Q', 0, 0xe30000000007000f, 0, 0)
    job = json.dumps({'key': '0123456789abcdef', 'stage': 'cs', 'local': [1, 1, 1],
                      'shared': 0, 'c1': '', 'tex': {}, 'img': {}, 'attr': {}, 'fout': {}}).encode()
    pointer = ctypes.c_void_p(); size = ctypes.c_int()
    status = call(code, len(code), job, len(job), ctypes.byref(pointer), ctypes.byref(size))
    if status != 0 or not pointer.value: raise RuntimeError('Synthetic translation failed')
    try:
        result = ctypes.string_at(pointer, size.value)
        version, spv, resources, reader = struct.unpack_from('<4I', result)
        assert version == 1 and 16 + spv + resources + reader == len(result)
        assert result[16:20] == struct.pack('<I', 0x07230203)
        assert isinstance(json.loads(result[16+spv:16+spv+resources]), dict)
    finally: lib.satchel_shader_translate_free(pointer)
    cases = [(None, 32, job, len(job)), (code, 31, job, len(job)),
             (code, 33, job, len(job)), (code, 16*1024*1024+8, job, len(job)),
             (code, 32, None, 1), (code, 32, job, 0),
             (code, 32, job, 1024*1024+1), (code, 32, b'{', 1),
             (code, 32, b'[]', 2)]
    for replacement in [{'stage': 'bogus'}, {'key': 'x'*16}, {'key': '0'*17}]:
        bad = json.loads(job); bad.update(replacement); payload = json.dumps(bad).encode()
        cases.append((code, 32, payload, len(payload)))
    for args in cases:
        pointer = ctypes.c_void_p(1); size = ctypes.c_int(123)
        status = call(*args, ctypes.byref(pointer), ctypes.byref(size))
        assert status != 0 and pointer.value is None and size.value == 0
    assert call(code, 32, job, len(job), None, ctypes.byref(size)) == -1
    assert call(code, 32, job, len(job), ctypes.byref(pointer), None) == -1
    lib.satchel_shader_translate_free(None)
    assert lib.satchel_shader_runtime_features() == 0
    print('Native facade: synthetic translation, 14 rejection cases, null free and dynamic-code-disabled checks passed')

if __name__ == '__main__': main()
