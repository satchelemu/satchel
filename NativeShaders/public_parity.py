#!/usr/bin/env python3
"""Public shader regression gate with source and product identity checks."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

from build_receipt import digest, verify
from public_corpus import corpus
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
LOCK = 'NativeShaders/public-parity-lock.json'
AOT_PRODUCTS = ['repro/host-publish/SatchelShaderProbe.dylib',
                'SatchelShaderProbe.framework/SatchelShaderProbe']



def corpus_manifest():
    return [{'name': row['name'], 'input_sha256': hashlib.sha256(
        json.dumps(row, sort_keys=True, separators=(',', ':')).encode()).hexdigest()} for row in corpus()]


def sources(root=ROOT):
    files = set()
    for folder, patterns in [('NativeShaders', ['*.swift', '*.h', '*.json', 'build*.py', 'public*.py']),
                             ('Tools/MaxwellShaderCompiler', ['*.cs', '*.csproj', '*.h', '*.json', '*.py', '*.Config']),
                             ('Tools/ShaderBridge', ['GameShaderBridge.*', 'ArchiveBridge.*', 'RuntimeShaderBridge.h'])]:
        for pattern in patterns:
            files.update((root / folder).glob(pattern))
    files.discard(root / LOCK)
    files.add(root / 'Tools/CapturedFlatCompiler/Test.c')
    return {str(p.relative_to(root)): digest(p) for p in sorted(files)}


def products(work, root=ROOT):
    result = {}
    groups = [('aot', 'aot', AOT_PRODUCTS)]
    for platform in ('macosx', 'iphoneos'):
        groups += [('cross', 'cross-' + platform, ['libGameShaderBridge.a'] +
                   (['libGameShaderBridge.dylib'] if platform == 'macosx' else [])),
                   ('archive', 'archive-' + platform, ['libShaderArchive.a'])]
    for component, folder, names in groups:
        verify(component, work / folder, names, root)
        for name in [*names, 'build-receipt.json']:
            result[folder + '/' + name] = digest(work / folder / name)
    return result


def reviewed(root=ROOT):
    lock = json.loads((root / LOCK).read_text())
    if lock.get('format') != 1 or lock.get('sources') != sources(root):
        raise RuntimeError('Public shader source checkpoint mismatch; review and regenerate the public baseline')
    if lock.get('corpus') != corpus_manifest():
        raise RuntimeError('Public shader corpus checkpoint mismatch')
    if set(lock.get('outputs', {})) != {row['name'] for row in corpus()}:
        raise RuntimeError('Public shader baseline inventory mismatch')
    return lock


def validate_proof(work, root=ROOT):
    lock = reviewed(root)
    proof = json.loads((work / 'public-parity/result.json').read_text())
    expected = dict(format=1, complete=True, differences=0,
                    lock_sha256=digest(root / LOCK), products=products(work, root),
                    outputs=lock['outputs'])
    if proof.get('complete') is not True or type(proof.get('differences')) is not int or proof != expected:
        raise RuntimeError('Public shader proof is stale or incomplete; rerun NativeShaders/public_parity.py')
    return {'kind': 'public-synthetic', 'lock_sha256': expected['lock_sha256'],
            'cases': len(lock['outputs']), 'proof_sha256': digest(work / 'public-parity/result.json')}


def run_corpus(work, output):
    spec = importlib.util.spec_from_file_location('translation_build', ROOT / 'NativeShaders/build-translation.py')
    builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    swift = [ROOT / 'NativeShaders' / (name + '.swift') for name in builder.SOURCES + builder.METRICS if name != 'TranslationHarness']
    aot = work / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
    executable = output / 'public-parity-harness'
    subprocess.run(['xcrun', '--sdk', 'macosx', 'swiftc', '-O', '-j', '2', '-import-objc-header',
                    ROOT / 'NativeShaders/PublicParityBridge.h', *swift,
                    ROOT / 'NativeShaders/PublicParityHarness.swift',
                    work / 'cross-macosx/libGameShaderBridge.a', '-lc++', aot,
                    '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-o', executable], check=True)
    rows = corpus()
    # Never print program bytes or translated payloads, including on failure.
    result = subprocess.run([executable], input=''.join(json.dumps(row) + '\n' for row in rows),
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError('Public shader harness failed with exit ' + str(result.returncode))
    try:
        replies = [json.loads(line) for line in result.stdout.splitlines()]
    except ValueError:
        raise RuntimeError('Public shader harness returned invalid JSON') from None
    if len(replies) != len(rows) or any('error' in row for row in replies):
        failures = [rows[i]['name'] for i, row in enumerate(replies) if 'error' in row and i < len(rows)]
        raise RuntimeError('Public shader translation failed: ' + ', '.join(failures))
    required = {'spirv', 'reader', 'resources', 'reflection', 'b'}
    if any(not required.issubset(row) for row in replies):
        raise RuntimeError('Public shader output inventory is incomplete')
    return {row['name']: reply for row, reply in zip(rows, replies)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--products', type=Path, help='default: SATCHEL_SHADER_DEPS, else <SATCHEL_DEPS_ROOT>/m5')
    parser.add_argument('--output', type=Path, help='harness folder; required unless --validate')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--candidate', action='store_true', help='Write a review candidate only, never a passing proof')
    mode.add_argument('--validate', action='store_true',
                      help='Check the existing proof against the lock, sources and product receipts; writes nothing')
    args = parser.parse_args()
    args.products = args.products or DEPENDENCY_PATHS['shader_deps']()
    if args.validate:
        # Read-only: no harness build and no write under <products>, so a check never disturbs a
        # shared root that other builds are staging from.
        result = validate_proof(args.products, ROOT)
        print('Public shader proof valid: ' + str(result['cases']) + ' synthetic requests, lock ' +
              result['lock_sha256'][:12] + ', proof ' + result['proof_sha256'][:12] + ' (nothing written)')
        return
    if args.output is None:
        parser.error('--output is required unless --validate is given')
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.candidate:
        destination = args.products / 'public-parity'; destination.mkdir(exist_ok=True)
        # A failed rerun cannot leave an older success available to staging.
        (destination / 'result.json').write_text(json.dumps({'format': 1, 'complete': False}) + '\n')
    before = sources()
    binary = None if args.candidate else products(args.products)
    lock = None if args.candidate else reviewed()
    outputs = run_corpus(args.products, args.output)
    if sources() != before or (binary is not None and products(args.products) != binary):
        raise RuntimeError('Shader inputs changed during public validation')
    if args.candidate:
        (args.output / 'candidate.json').write_text(json.dumps(dict(format=1, sources=before, corpus=corpus_manifest(), outputs=outputs), indent=2) + '\n')
        print('Public baseline candidate only: ' + str(len(outputs)) + ' synthetic requests; no proof issued')
        return
    if outputs != lock['outputs']:
        names = sorted(k for k in outputs if outputs[k] != lock['outputs'].get(k))
        raise RuntimeError('Public shader output checkpoint mismatch: ' + ', '.join(names))
    proof = dict(format=1, complete=True, differences=0, lock_sha256=digest(ROOT / LOCK),
                 products=binary, outputs=outputs)
    destination = args.products / 'public-parity'; destination.mkdir(exist_ok=True)
    temporary = destination / 'result.tmp'
    temporary.write_text(json.dumps(proof, indent=2) + '\n')
    temporary.replace(destination / 'result.json')
    validate_proof(args.products)
    print('Public shader parity passed: ' + str(len(outputs)) + ' synthetic requests, source and product checkpoints match')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
