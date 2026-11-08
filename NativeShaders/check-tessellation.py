#!/usr/bin/env python3
"""Mac GPU check of the tessellation route: tessellator invariants and the translated
public hull and domain rows drawn through vertex capture, hull, tessellator and domain.
Inputs are only the public synthetic corpus; the harness prints counts, never shader text."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

from public_corpus import corpus
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--products', type=Path, help='default: SATCHEL_SHADER_DEPS, else <SATCHEL_DEPS_ROOT>/m5')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.products = args.products or DEPENDENCY_PATHS['shader_deps']()
    args.output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('translation_build', ROOT / 'NativeShaders/build-translation.py')
    builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    names = [n for n in builder.SOURCES if n != 'TranslationHarness'] + builder.METRICS + ['Tessellator', 'TessellationHarness']
    aot = args.products / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
    executable = args.output / 'tessellation-harness'
    subprocess.run(['xcrun', '--sdk', 'macosx', 'swiftc', '-O', '-j', '2', '-import-objc-header', ROOT / 'NativeShaders/TranslationBridge.h',
                    *[ROOT / 'NativeShaders' / (n + '.swift') for n in names], args.products / 'cross-macosx/libGameShaderBridge.a', '-lc++',
                    aot, '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-o', executable], check=True)
    rows = [row for row in corpus() if row['job']['stage'] in ('hs', 'ds')]
    result = subprocess.run([executable], input=''.join(json.dumps(row) + '\n' for row in rows), capture_output=True, text=True, timeout=600)
    try:
        reply = json.loads(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        reply = {'passed': False, 'failure': 'no result (exit ' + str(result.returncode) + ')'}
    (args.output / 'result.json').write_text(json.dumps(reply, indent=2) + '\n')
    print(json.dumps(reply))
    if result.returncode or reply.get('passed') is not True:
        sys.exit(1)


if __name__ == '__main__':
    main()
