"""Read only development oracle. Never imported by the phone implementation."""
import ast
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

# The pinned engine snapshot (nxnative f8a5618d with the fork patches); its shader tools are the parity reference.
ENGINE = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
TOOLS = ENGINE / 'dm/gfx/tools'
# The game whose profile and shader packages a private check reads (default: Zelda TOTK 1.4.3).
PROFILE = Path(os.environ.get('SATCHEL_SHADER_PROFILE', ENGINE / 'dm/games/0100F2C0115B6000'))
PACKAGES = Path(os.environ.get('SATCHEL_SHADER_PACKAGES', Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'zelda/work/shader/pkg'))
# The .NET runtime that runs the engine's prebuilt translator (check-translator.py).
DOTNET_ROOT = Path(os.environ.get('SATCHEL_DOTNET_ROOT', DEPENDENCY_PATHS['deps_root']() / 'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/dotnet'))

def records(d):
    """The package's BTCDBTCD records, as the pinned record_index cuts them."""
    r = [m.start() for m in re.finditer(b'BTCDBTCD', d)]
    return [d[x:(r[k + 1] if k + 1 < len(r) else len(d))] for k, x in enumerate(r)]

def functions(names):
    source = TOOLS / 'dmsh_build.py'
    tree = ast.parse(source.read_text())
    selected = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
    if len(selected) != len(names): raise RuntimeError('Missing oracle functions')
    sys.path.insert(0, str(TOOLS))
    import dmsh_ampere, dmsh_meshstage
    scope = dict(collections=collections, os=os, sys=sys, re=re, struct=struct, json=json, dmsh_ampere=dmsh_ampere, dmsh_meshstage=dmsh_meshstage, records=records)
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(source), 'exec'), scope)
    return scope

def jobs():
    scope = functions({'vertex_ab', 'early_fragment_tests', 'tf_layout', 'job_for'})
    scope['FACTS'] = {k: json.loads(v) for k,v in (line.rstrip('\n').split('\t',1) for line in (PROFILE/'shader_facts.tsv').read_text().splitlines() if line and not line.startswith('#'))}
    rows = {}
    for line in (PROFILE/'shader_map.tsv').read_text().splitlines():
        cols = line.split('\t')
        if len(cols) > 4 and cols[0].endswith('.bin'): rows.setdefault(cols[3], (cols[0], int(cols[1])))
    return scope, rows

# A reference edit must never silently change the parity target.
_lock=json.loads(Path(__file__).with_name('source-lock.json').read_text())
for _name,_digest in _lock['oracle_sha256'].items():
    if hashlib.sha256((TOOLS/_name).read_bytes()).hexdigest()!=_digest:
        raise RuntimeError('Pinned shader oracle changed: '+_name)
