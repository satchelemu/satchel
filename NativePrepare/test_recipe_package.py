#!/usr/bin/env python3
"""Mutation tests through the actual Swift verifier, using a task-owned package clone."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--package', type=Path, required=True)
p.add_argument('--verifier', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
assert not a.output.exists() and not a.output.resolve().is_relative_to(root)
a.output.mkdir(parents=True)
fixture = a.output / 'package'
subprocess.run(['cp', '-cR', a.package, fixture], check=True)
original = json.loads((fixture / 'manifest.json').read_text())
assert original['format'] == 2

def run(manifest, accepted):
    (fixture / 'manifest.json').write_text(json.dumps(manifest))
    result = subprocess.run([a.verifier, fixture, root / 'App/NativeGameProfiles.json', a.output], capture_output=True, text=True)
    assert (result.returncode == 0) == accepted, result.stdout + result.stderr

run(original, True)
count = 1
for key, value in [('signingStatus', 'needs signature'), ('format', 3), ('shaderInputs', 'offline-imposter'), ('environment', {}), ('modules', ['main', '../sdk'])]:
    altered = copy.deepcopy(original); altered[key] = value; run(altered, False); count += 1
for key, value in [('schema', 2), ('titleID', '../escape'), ('systemResource', 2**64-1), ('usedMemory', 0), ('name', 'Injected\ngame_env\tBAD=1')]:
    altered = copy.deepcopy(original); altered['recipe'][key] = value; run(altered, False); count += 1
altered = copy.deepcopy(original); altered['preparedProfile']['environment']['DM_SAVE'] = '/outside'
run(altered, False); count += 1
altered = copy.deepcopy(original); altered['files']['profile.tsv']['sha256'] = '0'*64
run(altered, False); count += 1
# A changed profile payload keeps its declared digest, and must fail hashing.
profile = fixture / 'profile.tsv'; before = profile.read_bytes(); profile.write_bytes(before + b'# changed\n')
run(original, False); count += 1
profile.write_bytes(before)
# A symlink must never turn bound content into an outside-container read.
profile.unlink(); profile.symlink_to(a.package / 'profile.tsv')
run(original, False); count += 1
print(f'PASS: {count} actual format 2 verifier acceptance and mutation checks')
(a.output / 'results.json').write_text(json.dumps({'passed': count})+'\n')
