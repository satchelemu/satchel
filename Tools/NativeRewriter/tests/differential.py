#!/usr/bin/env python3
"""Development-only oracle harness. Private bytes never appear in diagnostics."""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode = True

U8 = C.POINTER(C.c_uint8)
class Segment(C.Structure):
    _fields_ = [('bytes', U8), ('size', C.c_size_t), ('offset', C.c_uint64)]
class Cut(C.Structure):
    _fields_ = [('offset', C.c_uint64), ('original', U8), ('replacement', U8), ('size', C.c_size_t), ('reason', C.c_char_p)]
class Obs(C.Structure):
    _fields_ = [('offset', C.c_uint64), ('number', C.c_uint32), ('original_word', C.c_uint32), ('reason', C.c_char_p)]
class MatchFunction(C.Structure):
    _fields_ = [('size', C.c_uint64), ('fingerprints', C.POINTER(C.c_char_p)), ('fingerprint_count', C.c_size_t), ('cuts', C.POINTER(Cut)), ('cut_count', C.c_size_t)]
class MatchRule(C.Structure):
    _fields_ = [('name', C.c_char_p), ('functions', C.POINTER(MatchFunction)), ('function_count', C.c_size_t)]
class Profile(C.Structure):
    _fields_ = [('cuts', C.POINTER(Cut)), ('cut_count', C.c_size_t), ('observations', C.POINTER(Obs)), ('observation_count', C.c_size_t), ('capabilities', C.POINTER(C.c_char_p)), ('capability_count', C.c_size_t), ('recipe', C.c_uint32), ('match_rules', C.POINTER(MatchRule)), ('match_count', C.c_size_t)]
Cancel = C.CFUNCTYPE(C.c_int, C.c_void_p)
class Request(C.Structure):
    _fields_ = [('abi_version', C.c_uint32), ('segments', Segment * 3), ('bss_size', C.c_uint64), ('build_id', C.c_uint8 * 32), ('module_name', C.c_char_p), ('profile', Profile), ('max_image_bytes', C.c_size_t), ('max_patch_bytes', C.c_size_t), ('max_function_bytes', C.c_size_t), ('max_records', C.c_size_t), ('cancelled', Cancel), ('context', C.c_void_p)]
class Error(C.Structure):
    _fields_ = [('code', C.c_int), ('offset', C.c_int64), ('message', C.c_char * 192)]
class Buffer(C.Structure):
    _fields_ = [('data', U8), ('size', C.c_size_t)]
EXTS = ['patched.bin', 'fixups.tsv', 'gates.tsv', 'sites.tsv', 'cuts.tsv', 'obs.tsv', 'changed.tsv']

def load_library(path):
    lib = C.CDLL(str(path))
    lib.nr_rewrite.argtypes = [C.POINTER(Request), C.POINTER(C.c_void_p), C.POINTER(Error)]
    lib.nr_rewrite.restype = C.c_int
    lib.nr_result_get.argtypes = [C.c_void_p, C.c_int]
    lib.nr_result_get.restype = Buffer
    lib.nr_result_free.argtypes = [C.c_void_p]
    return lib

def profile_tables(engine):
    tables = {k: {} for k in ('CUTS', 'OBS', 'CAPABILITIES', 'FRAME_STEP')}
    for path in [engine / 'dm/tools/sdk_patches.py', *sorted((engine / 'dm/games').glob('*/patches.py'))]:
        spec = importlib.util.spec_from_file_location('reviewed_profile', path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for k in tables:
            for bid, value in getattr(mod, k, {}).items():
                if bid in tables[k]:
                    raise RuntimeError('duplicate profile build claim')
                tables[k][bid] = value
    spec = importlib.util.spec_from_file_location('sdk_rules', engine / 'dm/tools/sdk_patches.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    tables['SDK_MATCH_CUTS'] = mod.MATCH_CUTS
    return tables

def native(lib, module, name, tables, *, cancel=False, bounds=None, profile=None):
    owned = []
    def buf(data):
        obj = (C.c_uint8 * len(data)).from_buffer_copy(data)
        owned.append(obj)
        return obj
    q = Request()
    q.abi_version = 1
    for i, (offset, size) in enumerate(module.segments):
        q.segments[i] = Segment(buf(module.image[offset:offset+size]), size, offset)
    q.bss_size = module.bss
    q.build_id[:] = module.build_id
    q.module_name = name.encode()
    q.max_image_bytes = 512 * 1024**2
    q.max_patch_bytes = 64 * 1024**2
    q.max_function_bytes = 4 * 1024**2
    q.max_records = 2_000_000
    if bounds:
        for k, v in bounds.items():
            setattr(q, k, v)
    bid = module.build_id[:16].hex()
    cuts = (profile or {}).get('cuts', tables['CUTS'].get(bid, []))
    obs = (profile or {}).get('obs', tables['OBS'].get(bid, []))
    caps = (profile or {}).get('caps', tables['CAPABILITIES'].get(bid, []))
    ca = (Cut * len(cuts))()
    for i, (a, old, new, why) in enumerate(cuts):
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        ca[i] = Cut(a, buf(old), buf(new), len(old), why.encode())
    oa = (Obs * len(obs))(*(Obs(a, n, int(w, 16), why.encode()) for a, n, w, why in obs))
    cap = (C.c_char_p * len(caps))(*(x.encode() for x in caps))
    q.profile = Profile(ca, len(ca), oa, len(oa), cap, len(cap), (1 if bid == '277178b7dba1b6d4949a84778d4abc58' else 0xffffffff) if bid in tables['FRAME_STEP'] and name == 'main' else 0, None, 0)
    rules = (profile or {}).get('matches', tables.get('SDK_MATCH_CUTS', {}).get(name, []) if bid not in tables['CUTS'] else [])
    ra = (MatchRule * len(rules))()
    for j, rule in enumerate(rules):
        fns = rule.get('functions', [rule])
        fa = (MatchFunction * len(fns))()
        owned.append(fa)
        for k, fn in enumerate(fns):
            fp = (C.c_char_p * len(fn['fingerprints']))(*(v.encode() for v in fn['fingerprints']))
            cc = (Cut * len(fn['cuts']))()
            for z, (a, old, new, why) in enumerate(fn['cuts']):
                old, new = bytes.fromhex(old), bytes.fromhex(new)
                cc[z] = Cut(a, buf(old), buf(new), len(old), why.encode())
            owned.extend([fp, cc])
            fa[k] = MatchFunction(fn['size'], fp, len(fp), cc, len(cc))
        ra[j] = MatchRule(rule['name'].encode(), fa, len(fa))
    q.profile.match_rules = ra
    q.profile.match_count = len(ra)
    callback = Cancel(lambda _: int(cancel() if callable(cancel) else cancel))
    q.cancelled = callback
    result, err = C.c_void_p(), Error()
    status = lib.nr_rewrite(C.byref(q), C.byref(result), C.byref(err))
    if status:
        assert not result.value, 'failure published a partial result'
        return None, dict(status=status, offset=hex(err.offset), error=err.message.decode())
    try:
        artifacts = {}
        for i, ext in enumerate(EXTS):
            b = lib.nr_result_get(result, i)
            artifacts[ext] = C.string_at(b.data, b.size)
        return artifacts, None
    finally:
        lib.nr_result_free(result)

def fixture(words, *, far=False):
    image = bytearray(0x3000)
    struct.pack_into('<II', image, 0, 0x14000040, 0x1000)
    struct.pack_into('<4siiiii', image, 0x1000, b'MOD0', 0x100, 0, 0, 0x40, 0x54)
    struct.pack_into('<BBBBii', image, 0x1040, 1, 0x1b, 3, 0x3b, 0x4c, 1)
    struct.pack_into('<ii', image, 0x104c, 0x100-0x1040, 0x1090-0x1040)
    # zR CIE: version 1, code alignment 1, data alignment -8, return register 30.
    struct.pack_into('<II', image, 0x1080, 12, 0)
    image[0x1088:0x1090] = bytes([1, ord('z'), ord('R'), 0, 1, 0x78, 30, 1])
    image[0x1090] = 0x1b
    # Keep CIE separate from the FDE.
    struct.pack_into('<i', image, 0x1050, 0x10a0-0x1040)
    struct.pack_into('<IIiI', image, 0x10a0, 16, 0x10a4-0x1080, 0x100-0x10a8, max(4*len(words), 0x100))
    for i, w in enumerate(words):
        struct.pack_into('<I', image, 0x100+4*i, w)
    header = bytearray(0x100)
    header[:4] = b'NSO0'
    for i in range(3):
        struct.pack_into('<III', header, 0x10+16*i, 0x100+0x1000*i, 0x1000*i, 0x1000)
    struct.pack_into('<I', header, 0x3c, 0x8000000 if far else 0x100)
    return header + image

def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return dict(offset=hex(i), oracle_size=len(a), native_size=len(b))
    if len(a) != len(b):
        return dict(offset=hex(min(len(a), len(b))), oracle_size=len(a), native_size=len(b))
    return None

def compare_case(args, lib, tables, nso, path, name, label, profile=None):
    root = args.output / label
    root.mkdir(parents=True, exist_ok=True)
    oracle = root / 'oracle'
    env = dict(os.environ, DM_REWRITE_J='1')
    for k in ('DM_GAME', 'DM_GAMES_DIR', 'DM_PATCH_FRONT', 'DM_SPIN_LIST'):
        env.pop(k, None)
    env['DM_GAME'] = '0100F2C0115B6000' if label.startswith('private-') else ''
    if profile is not None:
        bid = nso.read(path).build_id[:16].hex()
        pp = root/'profiles'/'synthetic'
        pp.mkdir(parents=True, exist_ok=True)
        text = '\n'.join(f'{key} = {value!r}' for key, value in {
            'CUTS': {bid: profile['cuts']} if 'cuts' in profile else {},
            'OBS': {bid: profile.get('obs', [])},
            'CAPABILITIES': {bid: profile.get('caps', [])},
            'MATCH_CUTS': {name: profile.get('matches', [])},
        }.items())
        (pp/'patches.py').write_text(text+'\n')
        env['DM_GAMES_DIR'] = str(pp.parent)
        env['DM_GAME'] = 'synthetic'
    stamp = {
        'input_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'source_lock_sha256': hashlib.sha256((Path(__file__).resolve().parents[1]/'profiles/source-lock.json').read_bytes()).hexdigest(),
        'profile_sha256': hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest(),
        'module_name': name, 'selected_profile': env['DM_GAME'],
    }
    stamp_path = root/'oracle-manifest.json'
    reuse = args.reuse_oracle and stamp_path.exists() and json.loads(stamp_path.read_text()) == stamp and all((oracle/f'{name}.{ext}').exists() for ext in EXTS)
    p = subprocess.CompletedProcess([], 0) if reuse else subprocess.run([sys.executable, str(args.engine/'dm/tools/patch_nso.py'), str(path), str(oracle), name], env=env, capture_output=True)
    # Oracle failures can include original bytes. Never print or persist stdout/stderr.
    if p.returncode:
        return dict(case=label, oracle_exit=p.returncode, result='oracle rejected (byte-bearing diagnostics suppressed)')
    stamp_path.write_text(json.dumps(stamp, indent=2)+'\n')
    try:
        module = nso.read(path)
    except Exception:
        return dict(case=label, result='input loader rejected (byte-bearing diagnostics suppressed)')
    got, err = native(lib, module, name, tables, profile=profile)
    if err:
        return dict(case=label, result='native rejected', **err)
    dest = root/'native'
    dest.mkdir(exist_ok=True)
    differences = {}
    for ext, data in got.items():
        (dest/f'{name}.{ext}').write_bytes(data)
        d = first_diff((oracle/f'{name}.{ext}').read_bytes(), data)
        if d:
            differences[ext] = d
    return dict(case=label, result='different' if differences else 'byte-identical', differences=differences, tables=6)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', type=Path, required=True)
    ap.add_argument('--library', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--private-exefs', type=Path)
    ap.add_argument('--only', action='append')
    ap.add_argument('--reuse-oracle', action='store_true', help='reuse a prior successful oracle run, source lock still checked')
    args = ap.parse_args()
    args.output = args.output.resolve()
    repo = Path(__file__).resolve().parents[3]
    if args.output.is_relative_to(repo):
        ap.error('outputs must be outside the repository')
    lock = json.loads((Path(__file__).resolve().parents[1]/'profiles/source-lock.json').read_text())
    for relative, digest in lock['sources'].items():
        if hashlib.sha256((args.engine/relative).read_bytes()).hexdigest() != digest:
            ap.error(f'oracle source drift: {relative}')
    sys.path.insert(0, str(args.engine/'dm/tools'))
    import nso
    lib = load_library(args.library)
    tables = profile_tables(args.engine)
    cases = {
        'plain': [0x91000652, 0x8b120000, 0xd65f03c0],
        'system': [0xd53bd040, 0xd51bd041, 0xd53bd062, 0xd4000021, 0xd53be023, 0xd53be044, 0xd53b0025, 0xd65f03c0],
        'condition': [0xb4000052, 0xd503201f, 0xd65f03c0],
        'pcrel': [0x10000092, 0x90000012, 0xd65f03c0],
        'window': [0x885f7d52, 0x11000652, 0x880b7d52, 0x35ffffab, 0xd65f03c0],
        'spin': [0xb9400020, 0x34ffffe0, 0xd65f03c0],
        'counter': [0x71000400, 0x54ffffe1, 0xd65f03c0],
        'far': [0xd4000021, 0xd65f03c0],
    }
    results = []
    for label, words in cases.items():
        if args.only and label not in args.only:
            continue
        args.output.mkdir(parents=True, exist_ok=True)
        path = args.output/f'{label}.nso'
        path.write_bytes(fixture(words, far=label=='far'))
        row = compare_case(args, lib, tables, nso, path, 'fixture', label)
        results.append(row)
        print(json.dumps(row), flush=True)
    if args.private_exefs:
        for name in ('main', 'subsdk0', 'sdk'):
            if args.only and name not in args.only:
                continue
            row = compare_case(args, lib, tables, nso, args.private_exefs/name, name, 'private-'+name)
            results.append(row)
            print(json.dumps(row), flush=True)
    (args.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    return int(any(r['result']!='byte-identical' for r in results))
if __name__ == '__main__':
    raise SystemExit(main())
