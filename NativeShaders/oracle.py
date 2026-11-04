"""Read only development oracle. Never imported by the phone implementation."""
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home().joinpath('SatchelDeps'))) / 'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/NX2Apple'
TOOLS = ENGINE / 'dm/gfx/tools'
PROFILE = Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'host/engine/dm/games/0100F2C0115B6000'
PACKAGES = Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'zelda/work/shader/pkg'

def functions(names):
    source = TOOLS / 'dmsh_build.py'
    tree = ast.parse(source.read_text())
    selected = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
    if len(selected) != len(names): raise RuntimeError('Missing oracle functions')
    scope = dict(os=os, sys=sys, re=re, struct=struct, json=json)
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(source), 'exec'), scope)
    return scope

def jobs():
    scope = functions({'vertex_ab', 'early_fragment_tests', 'tf_layout', 'job_for', 'records'})
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
