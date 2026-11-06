#!/usr/bin/env python3
"""Freeze reviewed upstream profile data. Development only, never run on the phone."""
import argparse
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('engine', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    sys.path.insert(0, str(args.engine/'dm/tools'))
    files = [args.engine/'dm/tools/sdk_patches.py', *sorted((args.engine/'dm/games').glob('*/patches.py'))]
    registry = {'schema': 1, 'builds': {}, 'sdk_match_cuts': {}, 'profile_match_cuts': {}}
    census = []
    for path in files:
        spec = importlib.util.spec_from_file_location('reviewed_profile', path)
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        for kind in ('CUTS', 'OBS', 'CAPABILITIES', 'FRAME_STEP'):
            for bid, data in getattr(m, kind, {}).items():
                row = registry['builds'].setdefault(bid, {})
                if kind in row:
                    raise ValueError('duplicate build claim')
                row[kind] = (1 if bid == '277178b7dba1b6d4949a84778d4abc58' else 'unsupported') if kind == 'FRAME_STEP' else data
        rules = getattr(m, 'MATCH_CUTS', {})
        if path.name == 'sdk_patches.py':
            registry['sdk_match_cuts'] = rules
        elif rules:
            registry['profile_match_cuts'][path.parent.name] = rules
        funcs = [n.name for n in ast.walk(ast.parse(path.read_text())) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]
        if funcs:
            census.append({'profile': path.parent.name, 'functions': funcs})
    # Snowdrop's page pools (snowdrop_pools.py) follow the title's profile key, read as the pinned
    # profile_sizes reads it; a title without the key keeps upstream's default in the native rewriter.
    spec = importlib.util.spec_from_file_location('snowdrop_pools', args.engine/'dm/tools/snowdrop_pools.py')
    pools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pools)
    if pools.DEFAULT != (2048, 512, 1280):
        raise ValueError('snowdrop_pools.DEFAULT differs from the native rewriter')
    spec = importlib.util.spec_from_file_location('game', args.engine/'dm/games/game.py')
    game = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(game)
    registry['snowdrop_pools'] = {}
    for tsv in sorted((args.engine/'dm/games').glob('*/game.tsv')):
        words = game.Game(tsv.parent).words(pools.KEY, '')
        if words:
            registry['snowdrop_pools'][tsv.parent.name] = list(pools.parse(words, tsv))
    sources = sorted(set(files + list((args.engine/'dm/tools').glob('*.py'))))
    lock = {'engine_revision': subprocess.check_output(['git', '-C', str(args.engine), 'rev-parse', 'HEAD'], text=True).strip(), 'sources': {str(p.relative_to(args.engine)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}, 'executable_profiles': census}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'registry.json').write_text(json.dumps(registry, ensure_ascii=True, indent=2)+'\n')
    (args.output/'source-lock.json').write_text(json.dumps(lock, indent=2)+'\n')
    print(f"exported {len(registry['builds'])} builds, {len(census)} profiles with function definitions")
if __name__ == '__main__':
    main()
