#!/usr/bin/env python3
"""Validate owned frame copies and mailbox ordering against the installed Apple SDK."""
from pathlib import Path
import argparse, hashlib, json, platform, subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--dependency-root', type=Path, default=root)
args = parser.parse_args()
out = args.output.resolve()
dependency = args.dependency_root.resolve()
if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    raise SystemExit('Requires the installed Apple arm64 SDK.')
if out.exists() or out == root or root in out.parents:
    raise SystemExit('Use a fresh output directory outside the source tree.')
out.mkdir(parents=True)
files = [root/'App/SwitchRuntimeSurfaceState.swift', root/'App/SwitchRuntimeFrame.swift',
         root/'App/SwitchRuntimeSurface.swift', root/'Tests/runtime_frame.swift',
         root/'Tools/RuntimeFrame/Bridge.h', dependency/'Tools/GuestGraphics/GuestFrames.h',
         Path(__file__).resolve()]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
before = {str(p): sha(p) for p in files}
commands = []
def run(command, name):
    command = list(map(str, command))
    commands.append(command)
    result = subprocess.run(command, capture_output=True, text=True)
    (out/(name+'.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(name+' failed: '+str(out/(name+'.log')))
    return result.stdout.strip()

flags = ['-swift-version', '6', '-strict-concurrency=complete', '-warnings-as-errors',
         '-parse-as-library', '-import-objc-header', files[4], '-I', files[5].parent]
results = {}
for sanitizer in ['address', 'thread']:
    binary = out/('runtime-frame-'+sanitizer)
    run(['xcrun', 'swiftc', *flags, '-g', '-sanitize='+sanitizer,
         files[0], files[1], files[3], '-o', binary], sanitizer+'-compile')
    results[sanitizer] = run([binary], sanitizer+'-tests')
sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], 'ios-sdk')
run(['xcrun', 'swiftc', *flags, '-target', 'arm64-apple-ios17.0', '-sdk', sdk,
     '-emit-module', '-emit-module-path', out/'RuntimeFrame.swiftmodule',
     '-module-name', 'RuntimeFrame', files[0], files[1], files[2]], 'ios-compile')
assert before == {str(p): sha(p) for p in files}
manifest = {'sources': before, 'commands': commands, 'host_results': results,
            'ios_target': 'arm64-apple-ios17.0', 'strict_swift_version': 6,
            'scope': 'Original CoreGraphics pixel-copy/render, budget, epoch and concurrent mailbox checks; UIKit/SwiftUI compile only. No game/GPU or actual UIKit presentation claim.',
            'artifacts': {p.name: sha(p) for p in out.iterdir() if p.is_file()}}
(out/'validation-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
for value in results.values(): print(value)
print(out/'validation-manifest.json')
