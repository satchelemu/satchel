#!/usr/bin/env python3
"""Synthetic acceptance/rejection tests, including oracle-backed profile cases."""
import argparse
import copy
import json
import struct
import sys
sys.dont_write_bytecode = True
from pathlib import Path
from types import SimpleNamespace
from differential import fixture, load_library, native, profile_tables, compare_case


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', type=Path, required=True)
    ap.add_argument('--library', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--only', action='append')
    args = ap.parse_args()
    args.reuse_oracle = False
    args.output = args.output.resolve()
    if args.output.is_relative_to(Path(__file__).resolve().parents[3]):
        ap.error('outputs must be outside the repository')
    args.output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.engine/'dm/tools'))
    import nso
    import nso_match
    lib, tables = load_library(args.library), profile_tables(args.engine)
    count = 0
    def reject(label, module, expected, **kwargs):
        nonlocal count
        if args.only and label not in args.only:
            return
        result, err = native(lib, module, 'fixture', tables, **kwargs)
        assert result is None and err['status'] == expected, (label, err)
        count += 1
        print(json.dumps(dict(test=label, result='expected rejection', **err)), flush=True)
    m = nso.load(fixture([0xd4000021, 0xd65f03c0]))
    reject('cancel-before-work', m, 3, cancel=True)
    calls = [0]
    def cancel_later():
        calls[0] += 1
        return calls[0] >= 2
    reject('cancel-during-work', m, 3, cancel=cancel_later)
    reject('image-limit', m, 2, bounds={'max_image_bytes': 0x1000})
    reject('patch-limit', m, 2, bounds={'max_patch_bytes': 4})
    reject('ADRP-range', m._replace(bss=0xffffffff), 6)
    reject('missing-limits', m, 1, bounds={'max_records': 0})
    reject('implementation-ceiling', m, 2, bounds={'max_records': 8000001})
    bad = m._replace(segments=((4,0x1000), m.segments[1], m.segments[2]))
    reject('text-not-at-zero', bad, 1)
    bad = m._replace(segments=(m.segments[0], (0,0x1000), m.segments[2]))
    reject('overlapping-segments', bad, 1)
    bad = m._replace(image=bytearray(m.image))
    struct.pack_into('<I',bad.image,4,0xfffffffc)
    reject('MOD0-bounds',bad,1)
    reject('cut-mismatch',m,4,profile={'cuts': [(0x100,'00000000','1f2003d5','synthetic mismatch')]})
    reject('obs-mismatch',m,4,profile={'obs': [(0x104,7,'d503201f','synthetic mismatch')]})
    reject('obs-rewrite-conflict',m,4,profile={'obs': [(0x100,7,'d4000021','synthetic conflict')]})
    reject('obs-branch-relocation',m,4,profile={'obs': [(0x104,7,'d65f03c0','synthetic branch')]})
    # An explicitly reviewed cut is the only way to change a registered site.
    forms = {
        'profile-data': ([0xd503201f,0xd503201f,0xd65f03c0], {'cuts': [(0x100,'1f2003d5','200080d2','synthetic constant')], 'obs': [(0x104,7,'d503201f','synthetic observation')], 'caps':['SYNTHETIC']}),
        'sp-write': ([0x9100025f,0xd65f03c0], {}),
        'sp-writeback': ([0xf81f0ff2,0xd65f03c0], {}),
        'sp-word-writeback': ([0xb81fcff2,0xd65f03c0], {}),
        'sp-align': ([0xcb2263ff,0xd65f03c0], {}),
        'indirect-call': ([0xd63f0240,0xd65f03c0], {}),
        'bit-test': ([0x36000052,0xd503201f,0xd65f03c0], {}),
        'acquire-lock': ([0x885f7d40,0x35ffffe0,0x880b7d42,0x35ffffab,0xd65f03c0], {}),
        'window-exit': ([0x885f7d52,0x7100065f,0x54000081,0x880b7d52,0x35ffff8b,0xd503201f,0xd65f03c0], {}),
        # Original retry load/cmp/conditional branch joins the window's exclusive store.
        'retry-window': ([0x885f7d4f,0x6b1101ff,0x54000081,0x880b7d52,0x3500004b,0xd65f03c0,0x885f7d51,0x6b1101ff,0x54ffff60,0xd65f03c0], {}),
        'spin-fixed-store': ([0xb9000043,0xb9400020,0x34ffffc0,0xd65f03c0], {}),
        'spin-varying-store': ([0xb9000040,0xb9400020,0x34ffffc0,0xd65f03c0], {}),
    }
    for label,(words,profile) in forms.items():
        if args.only and label not in args.only:
            continue
        path=args.output/(label+'.nso')
        path.write_bytes(fixture(words))
        row=compare_case(args,lib,tables,nso,path,'main',label,profile)
        assert row['result']=='byte-identical',row
        count+=1
        print(json.dumps(row),flush=True)
    # A terminal pointer load and tail branch, at the end of the text function.
    tail_words = [0xd503201f] * (0xf00 // 4 - 2) + [0xf9400c12, 0xd61f0240]
    path = args.output / 'terminal-tail.nso'
    path.write_bytes(fixture(tail_words))
    row = compare_case(args, lib, tables, nso, path, 'main', 'terminal-tail')
    assert row['result'] == 'byte-identical', row
    count += 1
    print(json.dumps(row), flush=True)
    clobbered = tail_words[:-3] + [0xf9400c12, 0x52800012, 0xd61f0240]
    reject('tail-pointer-clobbered', nso.load(fixture(clobbered)), 5)
    reject('nonterminal-tail', nso.load(fixture([0xf9400c12, 0xd61f0240, 0xd65f03c0])), 5)
    # A complete, synthetic unwind function supplies both the native and Python fingerprints.
    path=args.output/'match.nso'
    path.write_bytes(fixture([0xd2800020,0xd65f03c0]))
    mm=nso.read(path)
    rule=nso_match.Image(mm.image,mm.text_end).recipe(0x100,'200080d2','400080d2','synthetic match')
    for label,rules in [('match-unique',[rule]),('match-ambiguous',[rule,rule]),('match-missing',[dict(rule,fingerprints=['0'*64])]),('match-original-mismatch',[dict(rule,cuts=[(0,'600080d2','400080d2','synthetic mismatch')])])]:
        if args.only and label not in args.only:
            continue
        row=compare_case(args,lib,tables,nso,path,'fixture',label,{'matches':rules})
        assert row['result']=='byte-identical',row
        count+=1
        print(json.dumps(row),flush=True)
    for label,rulebad in [('match-PC-relative-replacement',dict(rule,cuts=[(0,'200080d2','00000014','synthetic branch')])),('match-overlapping-cuts',dict(rule,cuts=rule['cuts']*2)),('match-invalid-fingerprint',dict(rule,fingerprints=['xyz']))]:
        reject(label,mm,1,profile={'matches':[rulebad]})
    # Explicit unsupported families must never publish a plausible partial rewrite.
    bad=nso.load(fixture([0xd61f0240,0xd65f03c0]))
    reject('unknown-x18-jump',bad,5)
    bad=m._replace(image=bytearray(m.image))
    bad.image[0x1200:0x1210]='Binned3\0'.encode('utf-16-le')
    reject('unported-Unreal-allocator',bad,5)
    print(json.dumps({'passed':count}),flush=True)
    (args.output/'contracts.json').write_text(json.dumps({'passed':count},indent=2)+'\n')
if __name__=='__main__':
    main()
