#!/usr/bin/env python3
"""Compile and read back synthetic GPU clears through the diagnostic Mac overlay."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ['SATCHEL_OUT']) / 'gpu-mac'
ENGINE = OUT / 'engine'
CHECKS = OUT / 'checks'
CHECKS.mkdir(exist_ok=True)
if shutil.disk_usage(OUT).free < 35.5 * 1024**3:
    raise SystemExit('GPU check blocked: preserving the 35 GiB reserve')
tests = ENGINE / 'dm/gfx/tests'
env = dict(os.environ, MAKEFLAGS='-j2', PYTHONDONTWRITEBYTECODE='1')
dry = subprocess.check_output(['make', '-n', '-B', 'GAME=0100F2C0115B6000', 't_clear_ds'],
                              cwd=tests, env=env, text=True)
tokens = next(shlex.split(line) for line in dry.splitlines() if ' -o t_clear_ds ' in line)
output = CHECKS / 'gpu-clear'
tokens[tokens.index('-o') + 1] = str(output)
tokens[tokens.index('t_clear_ds.m')] = str(ROOT / 'NativeHost/graphics/gpu-clear-check.m')
tokens += ['-I' + str(ENGINE / 'dm/gfx/nvn')]
subprocess.run(['nice', '-n', '10', *tokens], cwd=tests, env=env, check=True)
for counters in ['-1', 'every:1']:
    print('DM_NVN_GPUPROF=' + counters, flush=True)
    subprocess.run([str(output)], env=dict(env, DM_NVN_GPUPROF=counters,
                                          MTL_DEBUG_LAYER='1'), check=True)
