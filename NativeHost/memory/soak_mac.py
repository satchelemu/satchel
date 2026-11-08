#!/usr/bin/env python3
"""Long unattended Mac memory soak on an existing package, read in place.

By default it starts like the session soak (session/soak.py): the title's private save
backup, 90 seconds of confirmations from the first frame (Castlevania alternates a
touch on its Continue affordance), then the balanced movement loop (MOVEMENT), one
entry every six seconds, until the requested time has passed. With --route it plays
run_mac.py's frame-scripted route from a fresh start instead (same keys and capture
checkpoints, so those frames compare against another build), then the movement loop. It records the scene
memory TSV (every 60 frames), the cache census (every 600 frames), the guest VM TSV
and the host's resident size, plus a screenshot every five minutes for visual review.
Private saves are copies of the Mac backups the other tools use; nothing reads or
writes phone data. Numeric output and allowlisted diagnostics only.
"""
from pathlib import Path
import argparse
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
routes = runpy.run_path(str(Path(__file__).with_name('routes.py')))
SESSION_SOAK = runpy.run_path(str(ROOT / 'NativeHost/session/soak.py'))
MOVEMENT = SESSION_SOAK['MOVEMENT']
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('game', choices=['Castlevania', 'Odyssey', 'TOTK'])
p.add_argument('variant', help='Build folder under SATCHEL_OUT holding scene-harness')
p.add_argument('--seconds', type=int, required=True, help='Measured wall time, 60 to 7200')
p.add_argument('--label', required=True)
p.add_argument('--cache', default='shared', help='Private translation and pipeline cache name, kept across runs')
p.add_argument('--confirm-seconds', type=int, default=90, help='Confirmation phase length (session soak start); a longer one reads through opening dialogue')
p.add_argument('--route', action='store_true', help="Start with run_mac.py's frame-scripted route instead of the session soak's start")
p.add_argument('--leak-check', action='store_true', help='Scan the live host for unreferenced allocations before stopping')
p.add_argument('--allocation-stacks', action='store_true', help='With --leak-check: in-memory stack logging, keeping only symbolic host and system frames')
p.add_argument('--no-input', action='store_true', help="No confirmations or movement, as an idle title screen on the phone; the 'loop' starts at the first frame")
p.add_argument('--app-profile', action='store_true', help='The per-frame and event writers that the app turns on with -SatchelGraphicsProfile YES (PipelineSettings.m)')
p.add_argument('--audio', action='store_true', help='Real audio output path, muted, as the app runs it (the default is DM_AUDIO=0)')
p.add_argument('--window', action='store_true', help='Present to a window through the display link, as the app does (the default is offscreen); the window stays above others without taking focus')
p.add_argument('--snapshot-at', default='', help='Comma-separated run seconds at which to record heap totals by class and size; compare two with heap_diff.py')
a = p.parse_args()
if a.no_input and a.route: p.error('--no-input and --route exclude each other')
if a.snapshot_at and not re.fullmatch('[0-9]+(,[0-9]+)*', a.snapshot_at): p.error('Invalid --snapshot-at')
snapshot_times = sorted(int(value) for value in a.snapshot_at.split(',')) if a.snapshot_at else []
if not 30 <= a.confirm_seconds <= 900 or not 60 <= a.seconds <= 7200 or not re.fullmatch('[a-z0-9-]+', a.label) or not re.fullmatch('[a-z0-9-]+', a.variant) or not re.fullmatch('[a-z0-9-]+', a.cache):
    p.error('Invalid duration, label or variant')
keys = routes['ROUTES'][a.game] if a.route else ''
route_frames = routes['FRAMES'][a.game]
checkpoints = routes['checkpoints'](a.game, route_frames)
# With --route the movement loop starts after the last scripted step has been released.
route_end = max(int(step.split(':')[0][1:]) for step in keys.split(',')) + 300 if a.route else None
CONFIRM = a.confirm_seconds   # seconds of session-soak confirmations after the first frame (soak.py uses 90)

OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT): raise SystemExit('Outputs must be outside the checkout.')
host = OUT / a.variant / 'scene-harness'
runtime_inputs = json.loads((host.parent / 'build-inputs.json').read_text())
assert hashlib.sha256(Path(runtime_inputs['aotPath']).read_bytes()).hexdigest() == runtime_inputs['aotSHA256'], 'Shared translator changed since the host build'
work = OUT / 'soaks' / a.game / a.variant / a.label
work.mkdir(parents=True, exist_ok=False)
for name in ['save', 'temp', 'aoc', 'shots']: (work / name).mkdir()
package = BUILD / 'anygame/packages' / a.game
patched = BUILD / 'anygame/host-inputs' / a.game
# The session soak's private Mac save backups; run_mac.py's Castlevania route starts fresh.
if a.game != 'Castlevania' or not a.route: shutil.copytree(SESSION_SOAK['SAVES'][a.game], work / 'save', dirs_exist_ok=True)
for path in [host, package / 'romfs.img', package / 'profile.tsv', patched, package / 'dm/shader_map.tsv']: assert path.exists(), path
# The app's shader path at the current pin: the phone translator at runtime, with the
# translation and pipeline caches kept across runs like the app's runtime folder.
# Retained pack libraries belong to an older shader chain and are not used.
cache = OUT / 'caches' / a.cache / a.game
for name in ['translations', 'pipelines']: (cache / name).mkdir(parents=True, exist_ok=True)
(work / 'empty-msl').mkdir()
ctl = work / 'ctl.txt'; ctl.touch()
env = {k: v for k, v in os.environ.items() if not k.startswith(('DM_', 'SATCHEL_SCENE_', 'SATCHEL_CACHE_'))}
env.update(DM_PROFILE=str(package / 'profile.tsv'), DM_ROMFS=str(package / 'romfs.img'),
    DM_ROOT=str(work), DM_SAVE=str(work / 'save'), DM_TEMP=str(work / 'temp'), DM_AOC_DIR=str(work / 'aoc'),
    DM_MSL_DIR=str(work / 'empty-msl'), DM_NVN_SHMAP=str(package / 'dm/shader_map.tsv'),
    DM_PHONE_SHADERS='1', DM_PHONE_SHADER_CACHE=str(cache / 'translations'),
    DM_PSO_USER=str(cache / 'pso_user.tsv'), DM_PSO_ARCHIVE=str(cache / 'pipelines'),
    # PipelineSettings.m's default "exact" policy, no prebuild or prewarm at launch.
    DM_PSO_ASYNC='0', DM_PSO_SKIP='0', DM_PSO_AHEAD='0', DM_PSO_BUDGET_MS='4', DM_PSO_MTL4='1', DM_PSO_HYBRID='0',
    DM_PSO_PREBUILD='0', DM_PSO_PREPARE='0', DM_PSO_MANIFEST='off', DM_PSO_PREBUILD_THREADS='2', DM_PSO_PREPARE_THREADS='2',
    DM_ROMFS_MAPPED='0', DM_FPS_TARGET='0', DM_RENDER_SCALE='1', DM_OPMODE='1', DM_AUDIO='0',
    DM_NVN_TSV=str(work / 'frames.tsv'), DM_NVN_TSV_CALLS='0', DM_MEMSTAT=str(work / 'vm.tsv'),
    SATCHEL_SCENE_MEMORY=str(work / 'memory.tsv'), SATCHEL_CACHE_CENSUS=str(work / 'census.tsv'),
    DM_CTL=str(ctl), **({'DM_KEYS': keys} if keys else {}),
    DM_NVN_SHOT=','.join(map(str, checkpoints)) + ':' + str(work / 'shots/frame-%d.png'), DM_NVN_SHOT_OUT='1280x720')
(work / 'route.json').write_text(json.dumps({'game': a.game, 'variant': a.variant, 'frames': route_frames, 'keys': keys,
    'checkpoints': checkpoints, 'runtime': runtime_inputs, 'shaderMode': 'runtime', 'cache': a.cache, 'soakSeconds': a.seconds,
    'routeEnd': route_end, 'start': 'route' if a.route else 'no-input' if a.no_input else 'session-soak', 'appProfile': a.app_profile, 'audio': a.audio, 'window': a.window, 'confirmSeconds': None if a.route else CONFIRM, 'movement': MOVEMENT[a.game]}, indent=2) + '\n')
if a.audio: env.update(DM_AUDIO='1', DM_AUDIO_MUTE='1')
# Read only by a harness built with build_mac.py --services; the app sets both beside host.log.
env.update(DM_SERVICE_REPORT=str(work / 'services.log'), DM_MISSING_SERVICES=str(work / 'missing-services.json'))
if a.window: env.update(DM_WINDOW='1', DM_WINDOW_TOP='1')
if a.app_profile:
    # PipelineSettings.m's -SatchelGraphicsProfile YES. The frame TSV is already on above, with its call rows off.
    env.update(DM_SATCHEL_FRAME_TIMING='1', DM_SHADER_EVENTS=str(work / 'shader-events.tsv'), DM_PSO_SKIP_STATS=str(work / 'pipeline-skips.tsv'),
               DM_BOOTPROF='1', DM_SLOWFRAME='1', DM_PSO_WAITLOG='1', DM_NVN_WAITS='1', DM_NVN_GPUPROF='every:300')
# A long soak is this lane's one heavy job. Its name is not one the shared build
# exclusion waits for, so other lanes' builds are not held for an hour.
if a.allocation_stacks: env['MallocStackLogging'] = 'lite'
runner = host.with_name('memory-soak-host')
if runner.exists(): runner.unlink()
os.link(host, runner)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
start = time.monotonic(); wall_start = time.time()
child = subprocess.Popen(['nice', '-n', '10', str(runner), str(patched)], cwd=work, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors='replace')
inhibitor = subprocess.Popen(['caffeinate', '-i', '-w', str(child.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def last_frame():
    """The newest complete memory sample's guest frame, read from the file's tail."""
    try:
        with (work / 'memory.tsv').open('rb') as file:
            file.seek(0, 2); size = file.tell(); file.seek(max(0, size - 4096))
            text = file.read().decode(errors='replace')
    except OSError:
        return 0
    rows = text.split('\n')[:-1]   # the part after the last newline may be mid-write
    for row in reversed(rows):
        field = row.split('\t')[0]
        if field.isdigit(): return int(field)
    return 0


progress = []
inputs = [0]
loop_start = [None]   # run seconds at which the movement loop began
stop = threading.Event()


def snapshot(seconds):
    """Class and size totals of the live heap: counts and type names only, never contents."""
    out = work / 'snapshots'; out.mkdir(exist_ok=True)
    scan = subprocess.run(['heap', '--sortBySize', '--showSizes', '--noContent', '--quiet', str(child.pid)], capture_output=True, text=True, errors='replace', timeout=900)
    (out / ('heap-%05d.txt' % seconds)).write_text(re.sub(r'0x[0-9a-fA-F]+', '<address>', scan.stdout))


def drive():
    last_input = last_shot = last_sample = 0.0
    first_frame = None; confirmations = 0
    while not stop.is_set() and child.poll() is None:
        now = time.monotonic() - start
        if now - last_sample >= 5:
            last_sample = now
            rss = subprocess.run(['ps', '-o', 'rss=', '-p', str(child.pid)], capture_output=True, text=True).stdout.strip()
            progress.append(dict(seconds=round(now, 3), wall=round(time.time() - wall_start, 3), frame=last_frame(),
                                 rss=int(rss) * 1024 if rss.isdigit() else 0))
            with (work / 'progress.tsv').open('a') as file:
                if file.tell() == 0: file.write('seconds\twall\tframe\trss\n')
                row = progress[-1]; file.write('%s\t%s\t%d\t%d\n' % (row['seconds'], row['wall'], row['frame'], row['rss']))
        frame = progress[-1]['frame'] if progress else 0
        if snapshot_times and now >= snapshot_times[0]: snapshot(snapshot_times.pop(0))
        if first_frame is None and frame > 0: first_frame = now
        if a.no_input:
            # As soak_summary.py --skip 300 does for a phone folder: boot and the title's first load are excluded.
            if first_frame is not None and loop_start[0] is None and now - first_frame >= 300:
                loop_start[0] = now
                (work / 'loop-start.json').write_text(json.dumps(dict(seconds=round(now, 3), frame=frame)) + '\n')
            if first_frame is not None and now - last_shot >= 300:
                with ctl.open('a') as file: file.write('shot ' + str(work / 'shots' / ('soak-%04d.png' % int(now // 60))) + '\n')
                last_shot = now
            if now >= a.seconds: break
            time.sleep(.25); continue
        if a.route: moving = frame >= route_end
        else: moving = first_frame is not None and now - first_frame >= CONFIRM
        if not a.route and first_frame is not None and not moving and now - last_input >= 6:
            key = ['A~0.5', 'A~0.5', 'T0.94x0.965~0.2'][confirmations % 3] if a.game == 'Castlevania' else 'A~0.5'
            with ctl.open('a') as file: file.write(key + '\n')
            confirmations += 1; last_input = now
        if moving and loop_start[0] is None:
            loop_start[0] = now
            (work / 'loop-start.json').write_text(json.dumps(dict(seconds=round(now, 3), frame=frame)) + '\n')
        if moving and now - last_input >= 6:
            with ctl.open('a') as file: file.write(MOVEMENT[a.game][inputs[0] % len(MOVEMENT[a.game])] + '\n')
            inputs[0] += 1; last_input = now
        if moving and now - last_shot >= 300:
            with ctl.open('a') as file: file.write('shot ' + str(work / 'shots' / ('soak-%04d.png' % int(now // 60))) + '\n')
            last_shot = now
        if now >= a.seconds: break
        time.sleep(.25)


driver = threading.Thread(target=drive); driver.start()
failures = []; omitted = [0]; diagnostics = {}; translated = [0]
seed = work / 'empty-msl' / 'b-26dc2fa9477c'   # the pinned engine's translation chain (PLAN.md, engine pin)
expected_seed = {'[RED] shader library ' + str(seed) + ' has no stamp (inputs.tsv): no full dmsh_build.py run made it as it is, so c1_full.tsv and cb_extent.tsv are not taken',
                 '[RED] shader library directory ' + str(seed) + ' does not exist: translation chain version ' + seed.name[2:] + ' of this source tree has not been built yet (dm/gfx/tools/dmsh_build.py)'}
seed_notes = []
patterns = [r'frame [0-9]+.*', r'memory up to frame [0-9]+:.*', r'\[RED\] (?:.*shader.*|.*pipeline.*|.*command buffer.*)', r'CRASH sig=[0-9]+', r'FATAL.*', r'DM_KEYS: .*', r'screenshot [0-9]+x[0-9]+.*',
            r'PhoneShader key=[0-9a-f]{16} stage=[0-5] translated', r'\[RED\] PhoneShader key=[0-9a-f]{16} stage=[0-5] prepare failed status=-?[0-9]+',
            r'PhoneShader key=[0-9a-f]{16} variant=[A-Za-z0-9_]+ compile failed code=-?[0-9]+']


def read():
    with (work / 'host-safe.log').open('w') as log:
        for line in child.stdout:
            if not line.startswith(('[dm]', '[nvn]', '[mtl]')) or re.search(r'OutputDebugString|hexdump|instruction bytes|\[guest\]|bytes below sp|stack scan', line): omitted[0] += 1; continue
            match = next((m for pattern in patterns if (m := re.search(pattern, line))), None)
            if not match: omitted[0] += 1; continue
            safe = match[0].split(' | x19=')[0]
            if safe.endswith(' translated'): translated[0] += 1; continue
            # Retain at most a bounded number of repeated progress lines.
            kind = re.sub(r'[0-9]+', '#', safe)[:80]
            diagnostics[kind] = diagnostics.get(kind, 0) + 1
            if diagnostics[kind] <= 200 or re.search(r'FATAL|CRASH|\[RED\]', safe): log.write(safe + '\n'); log.flush()
            # An empty seed library is the app's runtime-translation start: these two are expected once.
            if safe in expected_seed: seed_notes.append(safe); continue
            if re.search(r'FATAL|CRASH|\[RED\]|compile failed', safe) and len(failures) < 200: failures.append(safe)


reader = threading.Thread(target=read, daemon=True); reader.start()
driver.join()
leak = None
if a.leak_check and child.poll() is None:
    scan_start = time.monotonic()
    try:
        scan = subprocess.run(['leaks', '--noContent', '--groupByType', '--quiet', *(['--fullStacks', '--nosources'] if a.allocation_stacks else ['--nostacks']), str(child.pid)],
                              capture_output=True, text=True, timeout=180)
        summary = re.search(r'(\d+) leaks for (\d+) total leaked bytes', scan.stdout)
        leak = dict(exit=scan.returncode, summaryFound=summary is not None, leaks=int(summary[1]) if summary else None,
                    leakedBytes=int(summary[2]) if summary else None)
        if summary and int(summary[1]):
            begin = scan.stdout.rfind('Process ', 0, summary.start())
            metadata = re.sub(r'0x[0-9a-fA-F]+', '<address>', scan.stdout[begin if begin >= 0 else summary.start():])
            if a.allocation_stacks:
                metadata = runpy.run_path(str(ROOT / 'NativeHost/session/soak_diagnostics.py'))['allocation_metadata'](metadata, host='memory-soak-host')
            (work / 'leak-types.txt').write_text(metadata)
    except subprocess.TimeoutExpired:
        leak = dict(error='Allocation scan timed out after 180 seconds')
    leak['elapsed'] = round(time.monotonic() - scan_start, 3)
stop.set()
alive = child.poll() is None
if alive:
    child.terminate()
    try: child.wait(10)
    except subprocess.TimeoutExpired: child.kill(); child.wait()
reader.join(5); inhibitor.wait(timeout=5)
# Frame progress gaps. The first load after the confirmations (cold translation included)
# is reported separately: the play gap counts from two minutes after the loop starts.
play_from = (loop_start[0] or 0) + 120
max_gap = startup_gap = 0.0
last_change = progress[0]['seconds'] if progress else 0
for before, after in zip(progress, progress[1:]):
    if after['frame'] > before['frame']: last_change = after['seconds']
    if after['seconds'] >= play_from and last_change >= play_from: max_gap = max(max_gap, after['seconds'] - last_change)
    else: startup_gap = max(startup_gap, after['seconds'] - last_change)
elapsed = round(time.monotonic() - start, 2); wall = round(time.time() - wall_start, 2)
result = dict(game=a.game, variant=a.variant, label=a.label, seconds=a.seconds, elapsed=elapsed, wallElapsed=wall,
              uninterrupted=wall - elapsed < 5, aliveAtEnd=alive, exit=child.returncode, failures=failures, omitted=omitted[0],
              frames=progress[-1]['frame'] if progress else 0, maxFrameGapSeconds=round(max_gap, 3), startupGapSeconds=round(startup_gap, 3), movementInputs=inputs[0], loopStartSeconds=loop_start[0], translations=translated[0], expectedSeedDiagnostics=len(seed_notes),
              missingCheckpointShots=[f for f in checkpoints if not (work / 'shots' / ('frame-%d.png' % f)).is_file()],
              soakShots=sorted(path.name for path in (work / 'shots').glob('soak-*.png')), leakCheck=leak)
result['completed'] = (alive and not failures and result['uninterrupted'] and max_gap < 60 and not result['missingCheckpointShots']
                       and (a.no_input or inputs[0] >= max(1, (a.seconds - 600) // 12)) and (work / 'memory.tsv').is_file() and (work / 'census.tsv').is_file()
                       and (not a.leak_check or (leak and leak.get('exit') == 0 and leak.get('leaks') == 0)))
(work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['completed'] else 1)
