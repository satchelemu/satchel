#!/usr/bin/env python3
"""Run an allowed game on a frame-scripted route with private memory/capture output."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
import re
import runpy
import shutil
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path.home() / 'SatchelBuild'
p = argparse.ArgumentParser()
p.add_argument('game', choices=['Castlevania', 'Odyssey', 'TOTK'])
p.add_argument('variant', choices=['baseline', 'candidate'])
p.add_argument('--frames', type=int, default=None)
p.add_argument('--keys', default=None)
p.add_argument('--label', default='route')
p.add_argument('--shader-mode', choices=['pack', 'runtime'], default='pack')
a = p.parse_args()
if a.frames is None: a.frames = 9000 if a.game == 'Castlevania' else 7200
routes = runpy.run_path(str(Path(__file__).with_name('routes.py')))['ROUTES']
if a.keys is None: a.keys = routes[a.game]

OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT): raise SystemExit('Outputs must be outside the checkout.')
host = OUT / a.variant / 'scene-harness'
runtime_inputs = json.loads((host.parent / 'build-inputs.json').read_text())
assert hashlib.sha256(Path(runtime_inputs['aotPath']).read_bytes()).hexdigest() == runtime_inputs['aotSHA256'], 'Shared translator changed since the host build'
work = OUT / 'runs' / a.game / a.variant / a.label
work.mkdir(parents=True, exist_ok=False)
for name in ['save', 'temp', 'aoc', 'shots']: (work / name).mkdir()
package = BUILD / 'anygame/packages' / a.game
patched = BUILD / 'anygame/host-inputs' / a.game
libraries = {'Castlevania': BUILD / 'castlevania/runtime-shaders/cache', 'Odyssey': BUILD / 'games/odyssey/shader/cache', 'TOTK': BUILD / 'zelda/work/shader/cache'}
tables = {'Castlevania': BUILD / 'castlevania/package/Castlevania-1.0.0/dm', 'Odyssey': BUILD / 'games/odyssey/package/Odyssey-1.4.1/dm', 'TOTK': BUILD / 'host/package/TOTK-1.4.3/dm'}
saves = {'Odyssey': BUILD / 'oneapp/transfer/Documents/NativeRuntime/0100000000010000/save', 'TOTK': BUILD / 'host/saves/dmios-final/Documents/save'}
if a.game in saves: shutil.copytree(saves[a.game], work / 'save', dirs_exist_ok=True)
for path in [host, package / 'romfs.img', package / 'profile.tsv', patched, libraries[a.game], tables[a.game] / 'shader_map.tsv']: assert path.exists(), path
checkpoints = runpy.run_path(str(Path(__file__).with_name('routes.py')))['checkpoints'](a.game, a.frames)
env = {k: v for k, v in os.environ.items() if not k.startswith(('DM_', 'SATCHEL_SCENE_'))}
env.update(DM_PROFILE=str(package / 'profile.tsv'), DM_ROMFS=str(package / 'romfs.img'),
    DM_ROOT=str(work), DM_SAVE=str(work / 'save'), DM_TEMP=str(work / 'temp'), DM_AOC_DIR=str(work / 'aoc'),
    DM_MSL_DIR=str(libraries[a.game]), DM_NVN_SHMAP=str(tables[a.game] / 'shader_map.tsv'),
    DM_PSO_USER=str(work / 'pso_user.tsv'), DM_PSO_ARCHIVE=str(work / 'pipelines'),
    DM_PSO_PREBUILD='0', DM_PSO_PREPARE='0', DM_PSO_MANIFEST='off', DM_PSO_PREBUILD_THREADS='2', DM_PSO_PREPARE_THREADS='2',
    DM_FPS_TARGET='30' if a.game == 'TOTK' else '0', DM_RENDER_SCALE='1', DM_OPMODE='0',
    DM_NVN_TSV=str(work / 'frames.tsv'), DM_NVN_TSV_CALLS='0', DM_MEMSTAT=str(work / 'vm.tsv'),
    SATCHEL_SCENE_MEMORY=str(work / 'memory.tsv'), DM_KEYS=a.keys,
    DM_NVN_SHOT=','.join(map(str, checkpoints)) + ':' + str(work / 'shots/frame-%d.png'), DM_NVN_SHOT_OUT='1280x720')
expected_runtime_diagnostic = None
if a.shader_mode == 'runtime':
    assert json.loads((package / 'manifest.json').read_text())['format'] == 2
    seed_roots = list((package / 'msl').glob('b-*'))
    assert len(seed_roots) == 1
    seed = (seed_roots[0] / 'inputs.tsv').read_text()
    assert '# No compiled game shaders. Runtime capture only.' in seed
    assert all(not line or line.startswith('#') for line in seed.splitlines())
    assert not any((package / 'msl').rglob('*.metallib'))
    capture = work / 'shader-capture'; capture.mkdir()
    expected_runtime_diagnostic = ('[RED] shader library ' + str(seed_roots[0]) +
        ' has no stamp (inputs.tsv): no full dmsh_build.py run made it as it is, so c1_full.tsv and cb_extent.tsv are not taken')
    env.update(DM_PHONE_SHADERS='1', DM_PHONE_SHADER_CACHE=str(work / 'shader-cache'),
               DM_NVN_SHDUMP=str(capture), DM_MSL_DIR=str(package / 'msl'),
               DM_NVN_SHMAP=str(package / 'dm/shader_map.tsv'), DM_PSO_MTL4='1', DM_PSO_HYBRID='1')
(work / 'route.json').write_text(json.dumps({'game': a.game, 'variant': a.variant, 'frames': a.frames, 'keys': a.keys, 'checkpoints': checkpoints, 'runtime': runtime_inputs, 'shaderMode': a.shader_mode}, indent=2) + '\n')
# The shared app build exclusion recognizes this executable name too.
runner = host.with_name('shader-harness')
if runner.exists(): runner.unlink()
os.link(host, runner)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
start = time.monotonic()
child = subprocess.Popen(['nice', '-n', '10', str(runner), str(patched)], cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
reason = []
final_capture = threading.Event()
def monitor():
    while child.poll() is None:
        if final_capture.is_set(): reason.append('final frame captured'); break
        if time.monotonic() - start > 600: reason.append('600 second ceiling'); break
        time.sleep(.25)
    if child.poll() is None:
        child.terminate()
        try: child.wait(5)
        except subprocess.TimeoutExpired: child.kill()
watcher = threading.Thread(target=monitor); watcher.start()
failures = []; omitted = 0; translated = set(); expected_diagnostics = []
# Allowlist complete diagnostic forms, never guest debug strings or raw memory.
patterns = [r'frame [0-9]+.*', r'memory up to frame [0-9]+:.*', r'\[RED\] (?:.*shader.*|.*pipeline.*|.*command buffer.*)', r'CRASH sig=[0-9]+', r'FATAL.*', r'DM_KEYS: .*', r'screenshot [0-9]+x[0-9]+.*']
patterns += [r'PhoneShader key=[0-9a-f]{16} stage=[0-5] translated',
             r'\[RED\] PhoneShader key=[0-9a-f]{16} stage=[0-5] prepare failed status=-?[0-9]+',
             r'PhoneShader key=[0-9a-f]{16} variant=[A-Za-z0-9_]+ compile failed code=-?[0-9]+']
with (work / 'host-safe.log').open('w') as log:
    for line in child.stdout:
        if not line.startswith(('[dm]', '[nvn]', '[mtl]')) or re.search(r'OutputDebugString|hexdump|instruction bytes|\[guest\]|bytes below sp|stack scan', line): omitted += 1; continue
        match = next((m for pattern in patterns if (m := re.search(pattern, line))), None)
        if not match: omitted += 1; continue
        safe = match[0].split(' | x19=')[0]
        log.write(safe + '\n'); log.flush()
        if safe.startswith('screenshot ') and safe.endswith('/frame-%d.png ok' % a.frames): final_capture.set()
        if safe == expected_runtime_diagnostic: expected_diagnostics.append(safe)
        elif re.search(r'FATAL|CRASH|\[RED\]|compile failed', safe): failures.append(safe)
        if ' translated' in safe: translated.add(safe)
child.wait(); watcher.join()
result = dict(game=a.game, variant=a.variant, exit=child.returncode, seconds=round(time.monotonic()-start, 2), reason=reason, failures=failures, omitted=omitted,
              shots=[str(path) for path in sorted((work / 'shots').glob('*.png'))])
result['completed'] = reason == ['final frame captured'] and child.returncode in (0, -15, 143) and not failures
missing_shots = [frame for frame in checkpoints if not (work / 'shots' / ('frame-%d.png' % frame)).is_file()]
samples = []
if (work / 'memory.tsv').exists():
    with (work / 'memory.tsv').open() as file:
        samples = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file, delimiter='\t')]
result['missingShots'] = missing_shots
result['memorySamples'] = len(samples)
result['checkpoints'] = [{**min(samples, key=lambda row: abs(row['frame'] - frame)), 'checkpoint': frame}
                         for frame in checkpoints] if samples else []
result['completed'] &= not missing_shots and bool(samples) and samples[-1]['frame'] >= a.frames - 60
result['translations'] = len(translated)
result['expectedRuntimeDiagnostics'] = expected_diagnostics
if a.shader_mode == 'runtime':
    pairs = work / 'shader-capture/draw-pairs.tsv'
    result['capturedPairs'] = sum(bool(line) and not line.startswith('#') for line in pairs.read_text().splitlines()) if pairs.exists() else 0
    result['completed'] &= bool(translated) and len(expected_diagnostics) == 1 and result['capturedPairs'] > 0
(work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['completed'] else 1)
