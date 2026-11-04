#!/usr/bin/env python3
"""Mac synthetic service checks. Run under NativePrepare/guard.py."""
from pathlib import Path
import os
import json
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/anygame/services'))
if OUT.resolve().is_relative_to(ROOT): raise SystemExit('Service build outputs must be outside the worktree.')
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
results = []
def run(args):
    command = [str(x) for x in args]
    result = subprocess.run(command, capture_output=True, text=True)
    log = OUT / ('check-' + str(len(results) + 1) + '.log')
    log.write_text(result.stdout + result.stderr)
    results.append(dict(command=command, exit=result.returncode, log=str(log)))
    (OUT / 'check-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(result.stdout + result.stderr, end='', flush=True)
    result.check_returncode()
engine = Path.home() / 'SatchelDeps/NX2Apple/engine'
fsout = OUT / 'filesystem'; fsout.mkdir(exist_ok=True)
runpy.run_path(str(HERE / 'overlay.py'))['prepare_fs'](engine / 'dm/host/hle_fs.c', fsout)
(fsout / 'game_profile.h').write_text('/* Synthetic profile declared in test_fs_backend.c. */\n')
fsflags = ['-I' + str(fsout), '-I' + str(engine / 'dm/host'), '-Wno-unused-function']
fslink = [HERE / 'test_fs_backend.c', engine / 'dm/host/save_sync.c', engine / 'dm/host/hle_bigstack.c']
contracts = json.loads((HERE / 'round7.json').read_text())
roots = {'IApplicationProxyService':'appletOE', 'IAllSystemAppletProxiesService':'appletAE', 'IApplicationRootService':'vi:u', 'AudioRendererManager':'audren:u', 'IParentalControlServiceFactory':'pctl', 'IStaticServiceForGlue':'time:u'}
expected = [(roots.get(kind, kind), command) for kind, commands in contracts.items() for command in commands]
(OUT / 'round7-contracts.inc').write_text('static struct { const char *kind; unsigned command, hits[2]; } system_contracts[] = {\n' + ''.join('    {"%s", %du, {0,0}},\n' % entry for entry in expected) + '};\n')
contracts8 = json.loads((HERE / 'round8.json').read_text())
(OUT / 'round8-contracts.inc').write_text('static struct { const char *kind; unsigned command, hits[2]; } round8_contracts[] = {\n' + ''.join('    {"%s", %du, {0,0}},\n' % (kind, command) for kind, commands in contracts8.items() for command in commands) + '};\n')
run(['nice', '-n', '10', 'xcrun', 'clang', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-I' + str(OUT), '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
     '-I' + str(HERE), *fsflags, HERE / 'test_ipc.c', *fslink, '-o', OUT / 'test-ipc'])
run([OUT / 'test-ipc', OUT])
run(['nice', '-n', '10', 'xcrun', 'swiftc', '-j', '2', '-num-threads', '2', ROOT / 'App/NativeGameProfile.swift', ROOT / 'App/NativeGameReport.swift',
     HERE / 'test_report.swift', '-o', OUT / 'test-report'])
run([OUT / 'test-report', OUT / 'report-fixtures'])
engine = Path.home() / 'SatchelDeps/NX2Apple/engine'
overlay = runpy.run_path(str(HERE / 'overlay.py'))['prepare'](engine / 'dm/host/hle_sf.c', OUT / 'adapter')
clock = runpy.run_path(str(HERE / 'overlay.py'))['prepare_time'](engine / 'dm/host/hle_time.c', OUT / 'adapter')
# The engine header only needs profile declarations in this synthetic translation unit.
(OUT / 'adapter/game_profile.h').write_text('/* Synthetic test, no profile values used. */\n')
support = runpy.run_path(str(HERE / 'test_support.py'))['prepare'](engine, OUT / 'adapter')
run(['nice', '-n', '10', 'xcrun', 'clang', '-g', '-O1', '-Wall', '-Wextra', '-Wno-unused-function', '-DSATCHEL_SUPPORT_NO_MAIN',
     '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(OUT / 'adapter'),
     '-DSATCHEL_FS_EXTERNAL_GUEST', *fsflags, HERE / 'test_adapter.c', support, *fslink, '-o', OUT / 'test-adapter'])
run(['nice', '-n', '10', 'xcrun', 'clang', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
     '-fsanitize=address,undefined', '-fno-sanitize-recover=all', support, '-o', OUT / 'test-support'])
run([OUT / 'test-support', OUT / 'synthetic-dlc'])
report = OUT / 'adapter-services.log'; report.write_text('')
run([OUT / 'test-adapter', report])
summary = json.loads(Path(str(report) + '.json').read_text())
assert len(summary) == 6, summary
by_key = {(x['service'], x['interface'], x['command']): x for x in summary}
assert len(by_key) == len(summary)
unknown = by_key['set', 'ISettingsServer', 1234567]
assert unknown['count'] == 2 and unknown['firstSeenFrame'] == 37 and unknown['method'] is None
assert by_key['absent', 'unknown', 4294967295]['count'] == 2
assert by_key['acc:u0', 'IProfile', 4294967295]['count'] == 2
profile = by_key['acc:u0', 'IProfile', 10]
assert profile['method'] == 'GetImageSize' and profile['count'] == 4 and profile['firstSeenFrame'] == 42
audio = by_key['audren:u', 'AudioDevice', 20]
assert audio['count'] == 2 and audio['method'] == 'IsAudioDeviceOutputVolumeAutoTuneEnabled'
assert all(set(x) == {'service', 'interface', 'command', 'method', 'firstSeenFrame', 'count'} for x in summary)
reset_summary = json.loads(Path(str(report) + '.reset.json').read_text())
assert len(reset_summary) == 1 and reset_summary[0]['count'] == 1 and reset_summary[0]['firstSeenFrame'] == 99
print('PASS: persisted summary has one row per command, repeat counts, first frame, reference names and no payload fields')
print('PASS: synthetic commands, Swift report and actual engine adapter checks')
