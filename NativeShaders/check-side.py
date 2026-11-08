#!/usr/bin/env python3
"""Check the side library source (<key>_bx) against the pinned engine's own tool.

For every pixel shader of the public synthetic corpus, the Swift port (ShaderVariants.side) must
produce the compile unit that dm/gfx/tools/dmsh_side.py makes from the same base source, byte
for byte, with the same entry point lists; a shader the tool refuses must get no side source.
Each side source is then compiled with the Mac's Metal compiler and its entry points listed.
Synthetic inputs only; nothing here reads a game."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'NativeShaders'))
from public_corpus import corpus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--products', type=Path, help='default: SATCHEL_SHADER_DEPS, else <SATCHEL_DEPS_ROOT>/m5')
    parser.add_argument('--engine', type=Path, help='default: <SATCHEL_DEPS_ROOT>/NX2Apple/engine')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.products = args.products or DEPENDENCY_PATHS['shader_deps']()
    args.engine = args.engine or DEPENDENCY_PATHS['engine']()
    args.output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('translation_build', ROOT / 'NativeShaders/build-translation.py')
    builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    swift = [ROOT / 'NativeShaders' / (name + '.swift') for name in builder.SOURCES + builder.METRICS if name != 'TranslationHarness']
    aot = args.products / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
    executable = args.output / 'side-harness'
    subprocess.run(['nice', '-n', '10', 'xcrun', '--sdk', 'macosx', 'swiftc', '-O', '-j', '2', '-import-objc-header',
                    ROOT / 'NativeShaders/PublicParityBridge.h', *swift, ROOT / 'NativeShaders/SideHarness.swift',
                    args.products / 'cross-macosx/libGameShaderBridge.a', '-lc++', aot,
                    '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-o', executable], check=True)
    sources = args.output / 'sources'
    sources.mkdir(exist_ok=True)
    for old in sources.glob('*.metal'): old.unlink()
    rows = [row for row in corpus() if row['job']['stage'] == 'ps']
    result = subprocess.run([executable, sources], input=''.join(json.dumps(row) + '\n' for row in rows),
                            capture_output=True, text=True, timeout=120)
    if result.returncode: raise SystemExit('Side harness failed with exit ' + str(result.returncode))
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    if len(replies) != len(rows) or any('error' in reply for reply in replies):
        raise SystemExit('Side harness: translation failed for ' + ', '.join(row['name'] for row, reply in zip(rows, replies) if 'error' in reply))
    # The pinned tool: pure text functions of the engine's shader chain.
    tools = args.engine / 'dm/gfx/tools'
    sys.path.insert(0, str(tools))
    import dmsh_side
    made = alpha = logic = 0
    for row, reply in zip(rows, replies):
        name = row['name']
        base = (sources / (name + '.b.metal')).read_text()
        unit, info = dmsh_side.unit(base)
        side = sources / (name + '.bx.metal')
        if unit is None:
            if reply['side'] or side.exists(): raise SystemExit(name + ': the tool makes no side source (' + info['why'] + '), the port made one')
            continue
        if not side.exists(): raise SystemExit(name + ': the tool makes a side source, the port made none')
        if side.read_text() != unit: raise SystemExit(name + ': side source differs from dmsh_side.unit')
        if reply.get('alpha', []) != info['alpha'] or reply.get('logic', []) != info['logic']:
            raise SystemExit(name + ': entry point lists differ from dmsh_side.unit')
        # The unit must compile, and the library must hold every entry point the reflection names.
        air, library = args.output / (name + '.air'), args.output / (name + '.metallib')
        subprocess.run(['xcrun', '--sdk', 'macosx', 'metal', '-std=metal3.2', '-w', '-c', side, '-o', air], check=True)
        subprocess.run(['xcrun', '--sdk', 'macosx', 'metallib', air, '-o', library], check=True)
        text = library.read_bytes()
        for entry in info['alpha'] + info['logic']:
            if entry.encode() + b'\0' not in text: raise SystemExit(name + ': the compiled side library lacks ' + entry)
        air.unlink(); library.unlink()
        made += 1; alpha += len(info['alpha']); logic += len(info['logic'])
    print(f'PASS: {made} of {len(rows)} public pixel shaders have a side source identical to dmsh_side.unit; '
          f'{alpha} alpha test and {logic} logic operation entry points compiled and found')


if __name__ == '__main__': main()
