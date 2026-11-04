#!/usr/bin/env python3
"""Compare native variants with the pinned oracle and proven private MSL corpus."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path.home() / 'SatchelBuild/m5'
TOOLS = Path.home() / 'SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/NX2Apple/dm/gfx/tools'
CORPUS = Path.home() / 'SatchelBuild/shaders/package/TOTK-1.4.3-src/msl/b-e65a4ebd3322'
sys.path.insert(0, str(TOOLS))
import dmsh_vin, dmsh_pull, dmsh_alpha, dmsh_logic, dmsh_fetch

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    work = ROOT / args.label
    work.mkdir(exist_ok=False)
    generated = work / 'current.metal'
    child = subprocess.Popen([ROOT / 'variant-harness'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    start = time.monotonic()
    counts = collections.Counter()
    differences = []
    modules = {'bv': dmsh_vin, 'bp': dmsh_pull, 'ba': dmsh_alpha, 'bl': dmsh_logic, 'bf': dmsh_fetch}
    try:
        files = sorted(p for p in CORPUS.glob('*.metal') if not p.name.endswith('_b.metal'))
        if args.limit: files = files[:args.limit]
        for index, path in enumerate(files):
            stem, kind = path.stem.rsplit('_', 1)
            base = CORPUS / (stem + '_b.metal')
            source = base.read_text()
            kwargs = {}
            bindings = []
            if kind == 'bf':
                bindings = json.loads((CORPUS / (stem + '_b.json')).read_text())['bindings']
                kwargs = {'bindings': bindings}
            expected = path.read_bytes()
            # The recovered corpus proved each source against its original metallib.
            oracle, info = modules[kind].variant(source, **kwargs)
            mapping_only = False
            if kind == 'bf' and (oracle is None or oracle.encode() != expected):
                oracle, info = modules[kind].variant(source, **kwargs, om_only_ok=True)
                mapping_only = True
            if oracle is None or oracle.encode() != expected:
                raise RuntimeError('Pinned oracle does not reproduce corpus variant: ' + path.name)
            request = {'input': str(base), 'output': str(generated), 'kind': kind, 'bindings': bindings, 'mappingOnly': mapping_only}
            child.stdin.write(json.dumps(request) + '\n'); child.stdin.flush()
            response = json.loads(child.stdout.readline())
            counts[kind + '_attempted'] += 1
            actual = generated.read_bytes() if response['status'] == 'ok' else b''
            metadata = info
            if isinstance(metadata, dict): metadata = {k: v for k,v in metadata.items() if v is not None}
            same_source = actual == expected
            same_info = response.get('info') == metadata
            counts[kind + '_source_equal'] += same_source
            counts[kind + '_metadata_equal'] += same_info
            if not same_source or not same_info:
                differences.append({'name': path.name, 'status': response['status'], 'source_equal': same_source,
                    'metadata_equal': same_info, 'expected_sha256': hashlib.sha256(expected).hexdigest(),
                    'actual_sha256': hashlib.sha256(actual).hexdigest()})
            generated.unlink(missing_ok=True)
            if (index + 1) % 100 == 0:
                progress = {'done': index + 1, 'of': len(files), 'differences': len(differences), 'seconds': time.monotonic() - start}
                (work / 'progress.json').write_text(json.dumps(progress))
        summary = {'counts': counts, 'differences': differences, 'seconds': time.monotonic() - start}
        (work / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps({'counts': counts, 'differences': len(differences), 'seconds': summary['seconds']}))
        if differences: raise SystemExit(1)
    finally:
        child.stdin.close(); child.wait()
        generated.unlink(missing_ok=True)

if __name__ == '__main__': main()
