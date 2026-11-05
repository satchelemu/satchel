#!/usr/bin/env python3
"""Private Mac session soak. Retain numeric telemetry, never raw guest output."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import threading
import time
import sys

BUILD = Path.home() / 'SatchelBuild'
OUT = Path(os.environ.get('SESSION_MAC_OUT', BUILD / 'session/soak-mac'))
TITLES = ['Castlevania', 'Odyssey', 'TOTK']
TABLES = {'Castlevania': BUILD / 'castlevania/package/Castlevania-1.0.0/dm',
          'Odyssey': BUILD / 'games/odyssey/package/Odyssey-1.4.1/dm',
          'TOTK': BUILD / 'host/package/TOTK-1.4.3/dm'}
CACHES = {'Castlevania': BUILD / 'castlevania/runtime-shaders/cache',
          'Odyssey': BUILD / 'games/odyssey/shader/cache',
          'TOTK': BUILD / 'zelda/work/shader/cache'}
SAVES = {'Castlevania': BUILD / 'castlevania/run/save-display-gameplay',
         'Odyssey': BUILD / 'host/saves/before-oneapp-2049/0100000000010000/save',
         'TOTK': BUILD / 'host/saves/dmios-final/Documents/save'}


def manifest(folder):
    digest = hashlib.sha256()
    files = 0
    size = 0
    for path in sorted(folder.rglob('*')):
        if path.is_file():
            digest.update(str(path.relative_to(folder)).encode())
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
                    size += len(block)
            files += 1
    return dict(files=files, bytes=size, sha256=digest.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, required=True)
    parser.add_argument('--titles', nargs='+', choices=TITLES, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--require-ready', action='store_true', help='Start each play timer after visual approval in its private play.ready file')
    parser.add_argument('--fixed-slots', action='store_true', help='Anchor stop requests to fixed wall-clock slots, including prior teardown and loading')
    parser.add_argument('--leak-check', action='store_true', help='Scan allocation metadata after the final teardown')
    parser.add_argument('--allocation-stacks', action='store_true', help='Keep only symbolic allocation stacks, with in-memory stack logging')
    parser.add_argument('--capture-shaders', action='store_true', help='Capture executed shader inputs in the private run folder')
    parser.add_argument('--shader-inputs', type=Path, help='Private JSON mapping titles to replacement table and cache folders')
    parser.add_argument('--diagnostic-stop', action='store_true', help='Capture host-only stacks on a failed stop, without retrying it')
    parser.add_argument('--seed', type=Path, help='Prior private soak folder whose committed saves are copied')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 7200 or not re.fullmatch('[a-z0-9-]+', args.label):
        parser.error('Invalid duration or label')
    if args.fixed_slots and args.require_ready:
        parser.error('Fixed slots cannot use a gameplay start gate')
    if args.shader_inputs:
        inputs = json.loads(args.shader_inputs.read_text())
        for title, paths in inputs.items():
            if title not in TITLES or set(paths) != {'tables', 'cache'}:
                parser.error('Shader inputs require an authorized title, tables and cache')
            for path in paths.values():
                if not Path(path).resolve().is_relative_to((BUILD / 'session').resolve()):
                    parser.error('Replacement shader inputs must be in the private session folder')
            TABLES[title], CACHES[title] = Path(paths['tables']), Path(paths['cache'])
    if args.seed and (not args.seed.resolve().is_relative_to((BUILD / 'session').resolve()) or not args.seed.is_dir()):
        parser.error('Seed must be an existing private soak folder')
    run = OUT / (args.label + '-' + str(time.time_ns()))
    run.mkdir(parents=True)
    original_saves = {title: manifest(path) for title, path in SAVES.items()}
    (run / 'original-saves.json').write_text(json.dumps(original_saves, indent=2) + '\n')
    command = [str(OUT / 'mac-stop-probe')]
    for title in dict.fromkeys(args.titles):
        root = run / title
        for name in ['save', 'temp', 'aoc', 'archive', 'shots', 'package', 'capture']:
            (root / name).mkdir(parents=True)
        if args.seed:
            shutil.copytree(args.seed / title / 'save', root / 'save', dirs_exist_ok=True)
        else:
            shutil.copytree(SAVES[title], root / 'save', dirs_exist_ok=True)
        package = BUILD / 'anygame/packages' / title
        for name in ['profile.tsv', 'romfs.img']:
            (root / 'package' / name).symlink_to(package / name)
        (root / 'package/dm').symlink_to(TABLES[title])
        for path in [BUILD / 'anygame/host-inputs' / title / 'main.patched.bin',
                     CACHES[title], TABLES[title] / 'shader_map.tsv', package / 'romfs.img']:
            assert path.exists(), path
    for title in args.titles:
        command += [str(BUILD / 'anygame/host-inputs' / title), str(run / title / 'package'),
                    str(CACHES[title]), str(run / title)]
    (run / 'seed-saves.json').write_text(json.dumps(
        {title: manifest(run / title / 'save') for title in dict.fromkeys(args.titles)}, indent=2) + '\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
    env.update(SATCHEL_SOAK_SECONDS=str(args.seconds), DM_SAVE_SYNC='0', DM_AUDIO='0',
               DM_OPMODE='0', DM_PSO_PREBUILD_THREADS='2', DM_PSO_PREPARE='0')
    # Replacement reflection can change vertex variants for an existing key.
    # Never satisfy those requests from libraries compiled for the source pack.
    library_root = run / 'libraries' if args.shader_inputs else OUT / 'libraries'
    env.update(SATCHEL_MAC_CACHE_ROOT=str(library_root), SATCHEL_MAC_PYTHON=sys.executable,
               SATCHEL_MAC_COMPILER=str(Path(__file__).with_name('mac_shader_cache.py')))
    env['SATCHEL_MAC_SOURCE_CACHES'] = json.dumps([str(path) for path in CACHES.values()])
    if args.require_ready:
        env['SATCHEL_SOAK_READY'] = '1'
    if args.fixed_slots:
        env['SATCHEL_SOAK_FIXED_SLOTS'] = '1'
    if args.diagnostic_stop:
        env['SATCHEL_SOAK_FAILURE_HOLD'] = '1'
    if args.leak_check:
        env['SATCHEL_SOAK_LEAK_CHECK'] = '1'
    if args.allocation_stacks:
        env['MallocStackLogging'] = 'lite'
    if args.capture_shaders:
        env['SATCHEL_SOAK_CAPTURE'] = '1'
    leak_result = None
    start = time.monotonic()
    wall_start = time.time()
    process = subprocess.Popen(command, env=env, cwd=run, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors='replace')
    # Keep only this test's lifetime awake. Lid closure can still suspend a Mac,
    # so retain wall time as well as the host's active monotonic time.
    inhibitor = subprocess.Popen(['caffeinate', '-i', '-w', str(process.pid)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lines = queue.Queue(maxsize=2000)
    diagnostics = []
    omitted = [0]
    commits = [0]
    save_errors = [0]
    inputs_seen = [0]
    shader_errors = [0]
    red_errors = [0]
    cache_builds = [0]
    cache_cancellations = [0]
    cache_waits = []
    from soak_diagnostics import red_templates, red_category
    templates = red_templates(Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine')
    red_categories = {}
    unmapped_translations = {title: 0 for title in TITLES}
    cycle_shader_errors = [0] * len(args.titles)
    # stdout and stderr share the pipe. A guest logger can leave its prefix
    # before an otherwise intact numeric record. Extract only our record.
    numeric = re.compile(r'\[(session-proof|session-metrics|soak-resources)\] phase=[a-z-]+(?: [a-z_]+=[0-9]+)+')

    def read():
        reader_cycle = -1
        for line in process.stdout:
            line = line.strip()
            marker = re.search(r'\[session-proof\] phase=cycle result=([0-9]+)', line)
            if marker:
                reader_cycle = int(marker[1])
            if '[RED]' in line:
                red_errors[0] += 1
                category = red_category(line, templates)
                red_categories[category] = red_categories.get(category, 0) + 1
            if '[mac-cache] compiled library' in line:
                cache_builds[0] += 1
            if '[mac-cache] cancelled library' in line:
                cache_cancellations[0] += 1
            wait = re.search(r'\[mac-cache-wait\] phase=(compiler|lock|heavy) milliseconds=([0-9]+)', line)
            if wait:
                cache_waits.append(dict(cycle=reader_cycle, phase=wait[1], milliseconds=int(wait[2]),
                                       seconds=round(time.monotonic() - start, 3)))
            missing = re.search(r'shader ([0-9a-f]{16}) is not in the shader map', line)
            if missing:
                # Compare identifiers in memory. Retain counts, never shader bytes.
                for title, cache in CACHES.items():
                    if any((chain / (missing[1] + '_b.json')).is_file() for chain in cache.glob('b-*')):
                        unmapped_translations[title] += 1
            # Symbolic host diagnostics only. Never retain registers or payloads.
            for pattern in [r'CRASH sig=[0-9]+', r'UNIMPL [A-Za-z0-9_$:]+',
                            r'FATAL Mac shader compiler (?:launch failed|failed status=[0-9]+)',
                            r'Mac cache preparation failed: [A-Za-z]+(?: returned status [0-9]+)?',
                            r'\[session-host-stack\] [A-Za-z0-9_$:]+',
                            r'SVC (?:0x)?[0-9a-fA-F]+']:
                match = re.search(pattern, line)
                if match and len(diagnostics) < 100:
                    diagnostics.append(match[0])
            if '[RED]' in line and any(s in line for s in ['failed to load shader ', 'not in the shader map', 'program has no ', 'translation chain version', 'reflection conversion failed']):
                shader_errors[0] += 1
                if 0 <= reader_cycle < len(cycle_shader_errors):
                    cycle_shader_errors[reader_cycle] += 1
                if len(diagnostics) < 100:
                    diagnostics.append(next(s for s in ['failed to load shader ', 'not in the shader map', 'program has no ', 'translation chain version', 'reflection conversion failed'] if s in line).strip())
            if 'DM_CTL: press ' in line:
                inputs_seen[0] += 1
            if re.search(r'save commit #[0-9]+:', line):
                commits[0] += 1
            if 'save commit refused:' in line or 'save commit: clonefile' in line or 'save commit: atomic swap failed' in line:
                save_errors[0] += 1
            metric = numeric.search(line)
            if metric:
                lines.put(metric[0])
                if 'phase=stop-failed ' in metric[0] and args.diagnostic_stop:
                    from soak_diagnostics import write_process_samples
                    write_process_samples(run, process.pid, 'stop')
            elif 'FATAL' in line or 'CRASH sig=' in line:
                lines.put('[fatal]')
            else:
                omitted[0] += 1
        lines.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    rows = []
    saves = []
    cycle = -1
    begun = None
    last_input = 0
    last_shot = -1
    input_count = 0
    fatal = 0
    deadline = start + len(args.titles) * (args.seconds + 90 + (900 if args.require_ready else 0)) + 60
    fields = ['seconds', 'wall_seconds', 'cycle', 'title', 'phase', 'result', 'guest_threads', 'guest_mappings',
              'rss', 'frames', 'threads', 'fds', 'ports', 'ports_ok', 'footprint', 'compressed',
              'metal', 'malloc', 'memory_ok', 'save_commits', 'save_errors', 'inputs_seen', 'shader_errors', 'red_errors', 'cache_builds']
    pending = {}
    last_metric = start
    watchdog = None
    last_frames = None
    last_progress = start
    last_sample = start
    samples = []
    sample_worker = None

    def sample_stall(index, stalled):
        from soak_diagnostics import host_sample
        record = dict(cycle=index, stalledSeconds=round(stalled, 3))
        try:
            record.update(host_sample(process.pid))
        except subprocess.TimeoutExpired:
            record['error'] = 'Host symbol sampling timed out after 10 seconds'
        samples.append(record)
        (run / 'stall-stacks.json').write_text(json.dumps(samples, indent=2) + '\n')

    with (run / 'metrics.csv').open('w') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        while time.monotonic() < deadline:
            if time.monotonic() - last_metric > 90:
                watchdog = 'No numeric host telemetry for 90 seconds'
                if args.diagnostic_stop:
                    from soak_diagnostics import write_process_samples
                    write_process_samples(run, process.pid, 'watchdog')
                process.kill()
                break
            try:
                line = lines.get(timeout=.2)
            except queue.Empty:
                line = ''
            if line is None:
                break
            if line == '[fatal]':
                fatal += 1
            elif line:
                values = dict(re.findall(r'([a-z_]+)=([a-z-]+|[0-9]+)', line))
                phase = values.pop('phase')
                values = {k: int(v) for k, v in values.items()}
                if line.startswith('[session-proof]'):
                    if phase == 'cycle':
                        cycle = values['result']
                        last_frames = None
                        last_progress = time.monotonic()
                        begun = None
                        input_count = 0
                        last_shot = -1
                    if phase == 'started':
                        begun = time.monotonic()
                    pending = dict(seconds=round(time.monotonic() - start, 3),
                                   wall_seconds=round(time.time() - wall_start, 3), cycle=cycle,
                                   title=args.titles[cycle] if cycle >= 0 else '', phase=phase, **values)
                else:
                    pending.update(values)
                if line.startswith('[soak-resources]'):
                    last_metric = time.monotonic()
                    if phase in {'warming', 'running'}:
                        if pending['frames'] != last_frames:
                            last_frames = pending['frames']
                            last_progress = last_metric
                        if (last_metric - last_progress >= 20 and last_metric - last_sample >= 20
                                and (sample_worker is None or not sample_worker.is_alive())):
                            last_sample = last_metric
                            sample_worker = threading.Thread(target=sample_stall,
                                args=(cycle, last_metric - last_progress), daemon=True)
                            sample_worker.start()
                    pending.update(save_commits=commits[0], save_errors=save_errors[0], inputs_seen=inputs_seen[0],
                                   shader_errors=shader_errors[0], red_errors=red_errors[0], cache_builds=cache_builds[0])
                    rows.append(pending.copy())
                    writer.writerow(pending)
                    output.flush()
                    (run / 'diagnostics.json').write_text(json.dumps(dict(
                        shaderErrors=shader_errors[0], redErrors=red_errors[0],
                        cycleShaderErrors=cycle_shader_errors, unmappedTranslations=unmapped_translations,
                        redCategories=red_categories,
                        categories=diagnostics), indent=2) + '\n')
                    if phase == 'settled':
                        saves.append(dict(cycle=cycle, title=args.titles[cycle],
                                          **manifest(run / args.titles[cycle] / 'save')))
                        (run / 'save-manifests.json').write_text(json.dumps(saves, indent=2) + '\n')
                        begun = None
                        print(json.dumps(pending), flush=True)
                        if args.leak_check and len(saves) == len(args.titles):
                            scan_start = time.monotonic()
                            try:
                                scan = subprocess.run(['leaks', '--noContent', '--groupByType', '--quiet',
                                                       *(['--fullStacks', '--nosources'] if args.allocation_stacks else ['--nostacks']), str(process.pid)],
                                                      capture_output=True, text=True, timeout=30)
                                summary = re.search(r'(\d+) leaks for (\d+) total leaked bytes', scan.stdout)
                                leak_result = dict(exit=scan.returncode, summaryFound=summary is not None,
                                                   leaks=int(summary[1]) if summary else None,
                                                   leakedBytes=int(summary[2]) if summary else None)
                                if summary and int(summary[1]):
                                    # The installed tool suppresses allocation contents and stacks.
                                    # Retain type/count metadata to diagnose a failed scan.
                                    begin = scan.stdout.rfind('Process ', 0, summary.start())
                                    metadata = scan.stdout[begin if begin >= 0 else summary.start():]
                                    metadata = re.sub(r'0x[0-9a-fA-F]+', '<address>', metadata)
                                    if args.allocation_stacks:
                                        from soak_diagnostics import allocation_metadata
                                        metadata = allocation_metadata(metadata)
                                    (run / 'final-leak-types.txt').write_text(metadata)
                            except subprocess.TimeoutExpired:
                                leak_result = dict(error='Allocation scan timed out after 30 seconds')
                            leak_result['elapsed'] = round(time.monotonic() - scan_start, 3)
                            (run / 'final-leak-count.json').write_text(json.dumps(leak_result, indent=2) + '\n')
            now = time.monotonic()
            if begun is not None and cycle >= 0:
                elapsed = now - begun
                root = run / args.titles[cycle]
                if now - last_input >= 6:
                    # Balanced short movement limits drift into hazards during
                    # repeated routes. Zelda pairs movement before camera changes to avoid
                    # drifting toward a ledge. Saves and visual checks stay independent.
                    sequence = {
                        'Castlevania': ['LRight~0.6', 'Y~0.25', 'B~0.25', 'LLeft~0.6', 'Y~0.25', 'B~0.25'],
                        'Odyssey': ['LUp~0.6', 'Y~0.25', 'B~0.25', 'LDown~0.6', 'RRight~0.5', 'RLeft~0.5'],
                        'TOTK': ['LLeft~0.25\nLRight~0.25', 'X~0.25', 'Y~0.25', 'RRight~0.25\nRLeft~0.25', 'Y~0.25', 'X~0.25'],
                    }[args.titles[cycle]]
                    key = 'A~0.5' if elapsed < 90 else sequence[input_count % len(sequence)]
                    if args.titles[cycle] == 'Castlevania' and elapsed < 90:
                        # Its first-run Continue affordance also accepts touch.
                        key = ['A~0.5', 'A~0.5', 'T0.94x0.965~0.2'][input_count % 3]
                    with (root / 'ctl.txt').open('a') as ctl:
                        ctl.write(key + '\n')
                    input_count += 1
                    last_input = now
                shot = int(elapsed // 60)
                if elapsed >= 10 and shot != last_shot:
                    with (root / 'ctl.txt').open('a') as ctl:
                        ctl.write('shot ' + str(root / 'shots' / f'cycle-{cycle}-{shot}.png') + '\n')
                    last_shot = shot
        else:
            watchdog = 'Total run deadline exceeded'
            process.kill()
    if process.poll() is None:
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    reader.join(5)
    if sample_worker:
        sample_worker.join(11)
    inhibitor.wait(timeout=5)
    settled = [r for r in rows if r['phase'] == 'settled']
    result = dict(watchdog=watchdog, titles=args.titles, secondsPerTitle=args.seconds, exit=process.returncode,
                  elapsed=round(time.monotonic() - start, 2), completed=len(settled), fatal=fatal,
                  requireReady=args.require_ready,
                  fixedSlots=args.fixed_slots,
                  originalSaveUnchanged=all(manifest(path) == original_saves[title] for title, path in SAVES.items()),
                  saveCommits=commits[0], saveErrors=save_errors[0], inputsSeen=inputs_seen[0], shaderErrors=shader_errors[0],
                  redErrors=red_errors[0], unmappedTranslations=unmapped_translations,
                  redCategories=red_categories,
                  cacheBuilds=cache_builds[0],
                  cacheCancellations=cache_cancellations[0],
                  cacheWaits=cache_waits,
                  cycleShaderErrors=cycle_shader_errors,
                  saves=saves, diagnostics=diagnostics, omittedLines=omitted[0], directory=str(run))
    progress = []
    for index, title in enumerate(args.titles):
        running = [r for r in rows if r['cycle'] == index and r['phase'] == 'running']
        gaps = []
        last_frame = 0
        start_phase = 'play-start' if args.require_ready else 'started'
        first = next((r for r in rows if r['cycle'] == index and r['phase'] == start_phase), None)
        last_time = first['seconds'] if first else 0
        for row in running:
            if row['frames'] > last_frame:
                gaps.append(row['seconds'] - last_time)
                last_time = row['seconds']
                last_frame = row['frames']
        if running:
            gaps.append(running[-1]['seconds'] - last_time)
        progress.append(dict(cycle=index, title=title, frames=last_frame,
                             maxStallSeconds=max(gaps, default=args.seconds), samples=len(running),
                             startSeen=first is not None,
                             inputEvents=running[-1]['inputs_seen'] - first['inputs_seen'] if running and first else 0))
    result['progress'] = progress
    result['wallElapsed'] = round(time.time() - wall_start, 2)
    result['inactiveSeconds'] = round(max(0, result['wallElapsed'] - result['elapsed']), 2)
    result['uninterrupted'] = result['inactiveSeconds'] < 5
    result['resourceQueriesValid'] = bool(rows) and all(r['ports_ok'] == 1 and r['memory_ok'] == 1
                                         and r['threads'] > 0 and r['fds'] > 0 for r in rows)
    result['guestResourcesRetired'] = (len(settled) == len(args.titles) and
                                       all(r['guest_threads'] == 0 and r['guest_mappings'] == 0
                                           for r in settled))
    result['leakCheck'] = leak_result
    result['leakCheckPassed'] = (not args.leak_check or (leak_result is not None
                                  and leak_result.get('exit') == 0 and leak_result.get('summaryFound')
                                  and leak_result.get('leaks') == 0))
    result['passed'] = (result['exit'] == 0 and result['completed'] == len(args.titles)
                        and not fatal and not shader_errors[0] and not save_errors[0] and result['originalSaveUnchanged']
                        and result['resourceQueriesValid'] and result['guestResourcesRetired']
                        and result['uninterrupted'] and result['leakCheckPassed']
                        and all(p['startSeen'] and p['inputEvents'] >= max(1, args.seconds // 12)
                                and p['frames'] > 0 and p['maxStallSeconds'] < 60 for p in progress))
    (run / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
