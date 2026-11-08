#!/usr/bin/env python3
"""Bounded three-title regression using only existing allowed packages and safe diagnostics."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import threading
import time

OUT = Path(os.environ.get('SATCHEL_OUT', Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'anygame/services'))
if OUT.resolve().is_relative_to(Path(__file__).resolve().parents[2]): raise SystemExit('Boot outputs must be outside the worktree.')
BUILD = Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild')))
ROOT = BUILD / 'anygame'
CACHES = {'Castlevania': BUILD / 'castlevania/runtime-shaders/cache',
          'Odyssey': BUILD / 'games/odyssey/shader/cache', 'TOTK': BUILD / 'zelda/work/shader/cache'}
# The phone packages contain placeholder tables for runtime translation. These
# Mac hosts need the same offline tables used by the retained reference runs.
TABLES = {'Castlevania': BUILD / 'castlevania/package/Castlevania-1.0.0/dm',
          'Odyssey': BUILD / 'games/odyssey/package/Odyssey-1.4.1/dm',
          'TOTK': BUILD / 'host/package/TOTK-1.4.3/dm'}
patterns = [
    r'MacSourceShader result=(?:compiled|failed) code=-?[0-9]+',
    r'sf: [A-Za-z0-9_:.-]+',
    r'ServiceIPC service=[A-Za-z0-9_:.-]+ command=[0-9]+ status=(?:unknown|missing|stub) caller=[A-Za-z0-9_:.-]+\+0x[0-9a-fA-F]+',
    r'UNIMPL [A-Za-z0-9_$:]+',
    r'CRASH sig=[0-9]+(?: addr=[A-Za-z0-9_+x]+)? pc=[A-Za-z0-9_+x]+ lr=[A-Za-z0-9_+x]+',
    r'frame [0-9]+', r'SVC (?:0x)?[0-9a-fA-F]+',
    r'\[bootprof\] [A-Za-z0-9_ .=:+-]+',
    r'shader [0-9a-fA-F]{16} is not in the shader map',
    r'program has no (?:vertex|compute) shader',
]
parser = argparse.ArgumentParser()
parser.add_argument('--title', choices=list(CACHES), action='append')
parser.add_argument('--variant', choices=['baseline', 'services'], action='append')
parser.add_argument('--seconds', type=int, default=125)
parser.add_argument('--shots', default='300,1200,3000', help='Required frame captures; must include 300, 1200 and 3000')
parser.add_argument('--keys', help='Override the scripted route for one explicitly selected title')
parser.add_argument('--package', type=Path, help='A private prepared package for one explicitly selected title (for example a base without its update)')
parser.add_argument('--inputs', type=Path, help='Rewritten modules extracted from --package')
parser.add_argument('--count-sdk', action='store_true', help='Collect Mac-only SDK call counts')
parser.add_argument('--save-seed', type=Path, help='Copy TITLE/save folders into private run saves')
parser.add_argument('--route', action='store_true', help='Exercise the save/menu route (default)')
parser.add_argument('--idle-only', dest='route', action='store_false', help='Diagnostic idle run, not the save regression gate')
parser.set_defaults(route=True)
args = parser.parse_args()
requested_shots = sorted(set(int(x) for x in args.shots.split(',')))
if not {300, 1200, 3000}.issubset(requested_shots): parser.error('Required checkpoints are 300, 1200 and 3000.')
if args.keys is not None and len(args.title or []) != 1: parser.error('--keys requires exactly one --title.')
if (args.package is None) != (args.inputs is None) or (args.package and len(args.title or []) != 1): parser.error('--package and --inputs go together, with exactly one --title.')
if args.seconds < 120: parser.error('Long regression requires at least 120 seconds.')
routes = {'Castlevania': (Path(__file__).parents[1] / 'memory/castlevania-route.txt').read_text().strip(),
          'Odyssey': 'f900:A~f9,f1200:A~f9,f1500:A~f9,f2100:A~f9',
          'TOTK': 'f900:A~f9,f1200:A~f9,f1500:A~f9'}
if args.keys is not None: routes[args.title[0]] = args.keys
def save_hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}
patterns[:0] = [r'Round(?:8|9|11|12) service=[A-Za-z0-9_:.-]+ command=[0-9]+ result=0x[0-9a-f]+',
               r'game called nn::diag abort: Result [0-9]+, call site [A-Za-z0-9_]+\+0x[0-9a-f]+',
               r'save commit #[0-9]+', r'DM_KEYS: [A-Za-z0-9_ .,:~+\-]+']
results = {}
for title in args.title or ['Castlevania', 'Odyssey', 'TOTK']:
    for variant in args.variant or ['baseline', 'services']:
        work = OUT / 'boot' / (title + '-' + variant) / str(time.time_ns())
        for folder in ['save', 'temp', 'aoc', 'shots']: (work / folder).mkdir(parents=True, exist_ok=True)
        seed_hashes = None
        if args.save_seed:
            seed = args.save_seed / title / 'save'
            if not seed.is_dir(): raise SystemExit('Missing private save seed for ' + title)
            if any(p.is_symlink() for p in seed.rglob('*')): raise SystemExit('Save seed must not contain links')
            seed_hashes = save_hashes(seed)
            shutil.copytree(seed, work / 'save', dirs_exist_ok=True)
            assert save_hashes(work / 'save') == seed_hashes, 'Private save copy mismatch'
        package = args.package or ROOT / 'packages' / title
        host = OUT / 'mac' / ('dmhost-' + variant)
        patched = args.inputs or ROOT / 'host-inputs' / title
        for path in [host, patched, package / 'profile.tsv', package / 'romfs.img', CACHES[title], TABLES[title] / 'shader_map.tsv']: assert path.exists(), path
        env = {k:v for k,v in os.environ.items() if not k.startswith('DM_')}
        env.update(DM_PROFILE=str(package/'profile.tsv'), DM_ROMFS=str(package/'romfs.img'),
            DM_ROOT=str(work), DM_SAVE=str(work/'save'), DM_TEMP=str(work/'temp'),
            DM_MSL_DIR=str(CACHES[title]), DM_NVN_SHMAP=str(TABLES[title]/'shader_map.tsv'),
            DM_SF_TRACE='1' if variant == 'services' else '0', DM_KEYS=routes[title] if args.route else '',
            DM_CTL=str(work/'ctl.txt'), DM_BOOTPROF='1', DM_PSO_PREBUILD_THREADS='2', DM_OPMODE='0',
            DM_NVN_SHOT=','.join(map(str, requested_shots))+':'+str(work/'shots/frame-%d.png'), DM_NVN_SHOT_OUT='1280x720',
            DM_SERVICE_REPORT=str(work/'services.log'), DM_MISSING_SERVICES=str(work/'missing-services.json'))
        if args.count_sdk: env['DM_HLECOUNT'] = str(work / 'sdk-counts.tsv')
        (work/'ctl.txt').write_text(''); (work/'services.log').write_text('')
        (work/'missing-services.json').write_text('[]\n')
        start = time.monotonic(); child = subprocess.Popen(['nice','-n','10',str(host),str(patched)],
            cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
        bounded = []
        def stop():
            deadline = start + args.seconds
            while child.poll() is None and time.monotonic() < deadline: time.sleep(.5)
            if child.poll() is None:
                bounded.append(True); child.terminate()
                try: child.wait(5)
                except subprocess.TimeoutExpired: child.kill()
        watcher = threading.Thread(target=stop); watcher.start()
        flush_success = 0; flush_failure = 0; source_compiles = 0; shader_errors = {}; stub_hits = {}; fatal = 0; crash = 0; aborts = 0; save_commits = 0; space_queries = 0; gaps = set(); shaders = set(); omitted = 0
        with (work/'host-safe.log').open('w') as log:
            for line in child.stdout:
                if not line.startswith(('[dm]', '[nvn]', '[mtl]')): omitted += 1; continue
                if 'MacSourceShader result=compiled' in line: source_compiles += 1
                for phrase in ('MacSourceShader result=failed', 'failed to load shader', 'translation chain version', 'no reflection json', 'reflection conversion failed', 'not in the shader map'):
                    if phrase in line: shader_errors[phrase] = shader_errors.get(phrase, 0) + 1
                if 'Round9 service=SDK.FlushFile command=2 result=' in line:
                    if re.search(r'result=0x0\b', line): flush_success += 1
                    else: flush_failure += 1
                if 'save commit #' in line: save_commits += 1
                if re.search(r'Round8 service=SDK.GetFreeSpaceSize[.a-z-]* command=11 result=0x0\b', line): space_queries += 1
                if 'game called nn::diag abort:' in line: aborts += 1
                if 'FATAL' in line: fatal += 1; log.write('FATAL host stopped\n')
                if 'CRASH sig=' in line: crash += 1
                for pattern in patterns:
                    m = re.search(pattern,line)
                    if m:
                        log.write(m[0]+'\n'); log.flush()
                        if pattern.startswith(('ServiceIPC','UNIMPL')): gaps.add(m[0])
                        if pattern.startswith(('shader ', 'program ')): shaders.add(m[0])
                        break
                else: omitted += 1
        child.wait(); watcher.join()
        counts = work / 'sdk-counts.tsv'
        if args.count_sdk and counts.exists():
            for row in counts.read_text().splitlines()[1:]:
                kind, name, count = row.split('\t')
                if kind == 'STUB' and int(count): stub_hits[name] = stub_hits.get(name, 0) + int(count)
        summary = json.loads((work/'missing-services.json').read_text())
        shots = [str(p) for p in sorted((work/'shots').glob('frame-*.png'))]
        checkpoints = sorted(int(Path(p).stem.split('-')[1]) for p in shots)
        passed = bool(bounded) and child.returncode in (0,-15,143) and checkpoints==requested_shots and aborts==0 and fatal==0 and crash==0 and not summary and not any("status=unknown" in gap or "status=missing" in gap or gap.startswith("UNIMPL") for gap in gaps)
        seed_unchanged = seed_hashes is None or save_hashes(seed) == seed_hashes
        passed &= seed_unchanged and (not args.count_sdk or counts.is_file()) and not flush_failure
        passed &= not shader_errors.get('MacSourceShader result=failed', 0)
        if title == 'Castlevania' and shaders: passed = False
        if title == 'Castlevania' and args.route:
            passed &= save_commits > 0 and (variant == 'baseline' or space_queries > 0)
        results[title+'-'+variant] = dict(exit=child.returncode,seconds=round(time.monotonic()-start,2),bounded=bool(bounded),
            successfulFlushes=flush_success,failedFlushes=flush_failure,work=str(work),saveSeedUnchanged=seed_unchanged,sdkCounts=str(counts) if counts.exists() else None,stubHits=stub_hits,sourceShaderCompiles=source_compiles,shaderErrors=shader_errors,
            aborts=aborts,saveCommits=save_commits,successfulSpaceQueries=space_queries,route=args.route,checkpoints=checkpoints,screenshots=shots,missingSummary=summary,fatal=fatal,crash=crash,missing=sorted(gaps),shaderFailures=sorted(shaders),omitted=omitted,passed=passed)
        (work/'result.json').write_text(json.dumps(results[title+'-'+variant],indent=2)+'\n')
        (OUT/'boot-results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(title,variant,'PASS' if passed else 'FAIL',flush=True)
if not all(r['passed'] for r in results.values()): raise SystemExit(1)
print('PASS: all requested runs reached their required frames and duration with no crash or fatal diagnostics; visual gameplay needs separate review')
