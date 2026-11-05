#!/usr/bin/env python3
"""Check actual pinned NVN program code with synthetic controls and no shader dumps."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ['SATCHEL_OUT']) / 'control-check'
assert not OUT.resolve().is_relative_to(ROOT)
source = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
overlay = OUT / 'overlay'
for name in ['hle_nvn.c', 'nvn_mtl.m']:
    destination = overlay / 'dm/gfx/nvn' / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / 'dm/gfx/nvn' / name, destination)
for patch in ['NativeGames/runtime-capture.patch', 'NativeHost/phone-capture-lifetime.patch', 'NativeHost/phone-prefetch.patch']:
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', str(ROOT / patch)], cwd=overlay, check=True)
text = (overlay / 'dm/gfx/nvn/hle_nvn.c').read_text()
text = text[text.index('NVN(nvnProgramInitialize,'):text.index('// ---------- Sync / events')]
(OUT / 'phone-programs.inc').write_text(text)
command = ['nice', '-n', '10', 'xcrun', 'clang', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-Wno-deprecated-declarations', '-framework', 'CoreFoundation',
           '-I' + str(OUT), str(ROOT / 'NativeHost/test_phone_shader_controls.c'), '-o', str(OUT / 'check')]
subprocess.run(command, check=True)
for mode in ['phone', 'capture', 'offline']:
    subprocess.run([str(OUT / 'check'), mode], check=True)
# The same fixture must reject the previous coupling to diagnostic dumping.
old = text.replace('dump = getenv("DM_NVN_SHDUMP") != NULL || (phone && atoi(phone));',
                   'dump = getenv("DM_NVN_SHDUMP") != NULL;')
assert old != text
(OUT / 'phone-programs.inc').write_text(old)
command[-1] = str(OUT / 'negative')
subprocess.run(command, check=True)
result = subprocess.run([str(OUT / 'negative'), 'phone'], capture_output=True, text=True)
(OUT / 'negative.log').write_text(result.stderr)
assert result.returncode != 0 and 'program.sd[0].control != control' in result.stderr
(OUT / 'phone-programs.inc').write_text(text)
print('PASS: previous no-dump behavior fails the lifetime assertion')
