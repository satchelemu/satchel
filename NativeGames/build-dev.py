#!/usr/bin/env python3
"""Build the multi-game dev app using existing host and rewriter archives. No device actions."""
from pathlib import Path
import argparse, json, os, subprocess, sys
sys.dont_write_bytecode = True
from heavy import wait_for_slot
ROOT = Path(__file__).resolve().parents[1]
OUT = Path.home() / 'SatchelBuild/games'
HOST = Path.home() / 'SatchelBuild/host'
PREPARE = Path.home() / 'SatchelBuild/prepare'

def run(args): subprocess.run([str(a) for a in args], check=True)

def main():
    global OUT
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-root", type=Path, default=OUT)
    parser.add_argument("--prepare-project", type=Path, default=PREPARE / "project.json")
    args = parser.parse_args()
    OUT = args.out_root.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    wait_for_slot(OUT)
    project = json.loads(args.prepare_project.read_text())
    source = next(Path(r['path']).parent for r in project['targets']['SatchelDev']['sources'] if Path(r['path']).name == 'App')
    project = json.loads(json.dumps(project).replace(str(source), str(ROOT)))
    target = project['targets']['SatchelDev']
    lib = OUT / 'lib'; lib.mkdir(exist_ok=True)
    hosts = [Path(dep['framework']) for dep in target['dependencies'] if Path(dep.get('framework', '')).name == 'libSatchelNativeHost.a']
    assert len(hosts) == 1, 'Expected one native host archive in the preparation project'
    original = hosts[0]
    archive = lib / original.name
    if not archive.exists():
        run(['cp', '-c', original, archive])
        members = subprocess.check_output(['ar', '-t', archive], text=True).splitlines()
        assert members.count('SessionBridge.o') == 1
        run(['ar', '-d', archive, 'SessionBridge.o'])
        assert 'SessionBridge.o' not in subprocess.check_output(['ar', '-t', archive], text=True).splitlines()
    # Compile this worktree's session bridge once, outside the reused archive.
    for dep in target['dependencies']:
        if dep.get('framework') == str(original): dep['framework'] = str(archive)
    settings = target['settings']['base']
    settings['OTHER_LDFLAGS'] = [s.replace(str(original), str(archive)) for s in settings['OTHER_LDFLAGS']]
    settings['HEADER_SEARCH_PATHS'] += [str(HOST / 'engine/dm/host'), str(HOST / 'engine/dm/gfx/nvn/include')]
    target['sources'] += [{'path': str(ROOT / 'NativeHost/SessionBridge.m'), 'compilerFlags': '-fvisibility=default'}]
    target['info']['path'] = str(OUT / 'Info.plist')
    target['settings']['base']['SWIFT_OBJC_BRIDGING_HEADER'] = str(OUT / 'DevBridge.h')
    (OUT / 'DevBridge.h').write_text('#include "' + str(ROOT / 'NativeHost/Bridge.h') + '"\n#include "' + str(ROOT / 'NativePrepare/Registry.h') + '"\n#include "' + str(ROOT / 'NativePrepare/Signer/SigningIdentity.h') + '"\n')
    (OUT / 'project.json').write_text(json.dumps(project, indent=2) + '\n')
    run(['xcodegen', 'generate', '--spec', OUT / 'project.json', '--project', OUT])
    wait_for_slot(OUT)
    run(['nice', '-n', '10', 'xcodebuild', '-quiet', '-project', OUT / 'SatchelDev.xcodeproj', '-scheme', 'SatchelDev',
         '-configuration', 'Release', '-destination', 'generic/platform=iOS', '-derivedDataPath', OUT / 'DerivedData', '-jobs', '2', '-allowProvisioningUpdates', 'build'])
    sys.path.insert(0, str(ROOT / 'NativeHost'))
    import build
    build.OUT = OUT
    exec(compile((ROOT / 'NativeHost/verify_build.py').read_text(), str(ROOT / 'NativeHost/verify_build.py'), 'exec'), {'__name__': '__main__'})
    app = OUT / 'DerivedData/Build/Products/Release-iphoneos/SatchelDev.app'
    assert (app / 'NativeGameProfiles.json').read_bytes() == (ROOT / 'App/NativeGameProfiles.json').read_bytes()
    symbols = subprocess.check_output(['nm', app / 'SatchelDev'], text=True)
    assert symbols.count(' T _SatchelPrepareNativeSession\n') == 1
    for name in ['_nr_rewrite', '_cs_open', '_satchel_registry_profile']:
        assert any(line.endswith(' ' + name) for line in symbols.splitlines()), name
    print('PASS: multi-game registry bundled, one native session bridge, preparation symbols retained. No device actions.')

if __name__ == '__main__': main()
