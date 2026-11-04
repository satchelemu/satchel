#!/usr/bin/env python3
"""Real Metal lifetime regression, including an unchanged negative control."""
from pathlib import Path
import os
import runpy
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT): raise SystemExit('Outputs must be outside the checkout.')
source = Path.home() / 'SatchelDeps/NX2Apple/engine'
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
engine = OUT / 'engine'
if not engine.exists(): shutil.copytree(source, engine, ignore=shutil.ignore_patterns('.git', 'gen', '__pycache__'), symlinks=True)
reference = Path.home() / 'SatchelBuild/castlevania/engine/dm/host/gen'
for name in ['0100BA4026E94000/game_profile.h', 'msl-macosx/host_msl_gen.h']:
    path = engine / 'dm/host/gen' / name; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(reference / name, path)
tests = engine / 'dm/gfx/tests'
env = dict(os.environ, GAME='0100BA4026E94000', MAKEFLAGS='-j2', MTL_DEBUG_LAYER='1')
# Resolve the installed fixture's exact compile recipe, without rebuilding shaders.
dry = subprocess.check_output(['make', '-n', '-o', '../../host/gen/msl-macosx/host_msl_gen.h', 't_image_alias'], cwd=tests, env=env, text=True)
command = shlex.split(next(line for line in dry.splitlines() if line.startswith('clang ')))
command[command.index('t_image_alias.m')] = str(ROOT / 'NativeHost/memory/check.m')
command += ['-I' + str(tests)]
for variant in ['baseline', 'candidate']:
    metal = runpy.run_path(str(ROOT / 'NativeHost/memory/overlay.py'))['prepare'](source / 'dm/gfx/nvn/nvn_mtl.m', OUT / variant, reclaim=variant == 'candidate')
    shutil.copyfile(metal, engine / 'dm/gfx/nvn/nvn_mtl.m')
    executable = OUT / ('check-' + variant); command[command.index('-o') + 1] = str(executable)
    subprocess.run(['nice', '-n', '10', *command], cwd=tests, env=env, check=True)
    result = subprocess.run([executable], cwd=OUT, env=env, capture_output=True, text=True)
    (OUT / (variant + '.log')).write_text(result.stdout + result.stderr)
    print(result.stdout, end=''); print(result.stderr, end='')
    if variant == 'baseline':
        expected = ['pool finalization drops its finalized texture ghosts', 'retired pool texture is released after GPU completion', 'idle small scratch buffers respect the combined 64 MiB budget']
        assert result.returncode == 1 and all('FAIL: ' + item in result.stderr for item in expected), 'Negative control did not reproduce all retention failures'
        print('PASS: unchanged baseline reproduces the three lifetime assertions.')
    else:
        assert result.returncode == 0, 'Candidate lifetime or GPU pixel check failed'
print('PASS: scene resource lifetime and real GPU pixels, with baseline negative control.')
