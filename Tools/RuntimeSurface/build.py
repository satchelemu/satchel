#!/usr/bin/env python3
"""Original Foundation lifecycle tests and standalone iOS surface compilation."""
from pathlib import Path
import argparse, hashlib, json, platform, subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    raise SystemExit('Requires the installed Apple arm64 SDK.')
if out.exists() or out == root or root in out.parents:
    raise SystemExit('Use a fresh output directory outside the source tree.')
out.mkdir(parents=True)
files = [root/'App/SwitchRuntimeSurfaceState.swift', root/'App/SwitchRuntimeSurface.swift',
         root/'Tests/runtime_surface.swift', Path(__file__).resolve(),
         root/'App/SwitchRuntimeFrame.swift', root/'Tools/RuntimeFrame/Bridge.h',
         root/'Tools/GuestGraphics/GuestFrames.h']
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

flags = ['-swift-version', '6', '-strict-concurrency=complete', '-warnings-as-errors']
run(['xcrun', 'swiftc', *flags, '-parse-as-library', '-g', '-sanitize=thread',
     '-D', 'SURFACE_TESTING', files[0], files[2], '-o', out/'runtime-surface-tests'], 'host-compile')
result = run([out/'runtime-surface-tests'], 'host-tests')
sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], 'ios-sdk')
run(['xcrun', 'swiftc', *flags, '-parse-as-library', '-target', 'arm64-apple-ios17.0',
     '-sdk', sdk, '-emit-module', '-emit-module-path', out/'RuntimeSurface.swiftmodule',
     '-module-name', 'RuntimeSurface', '-import-objc-header', files[5], '-I', files[6].parent,
     files[0], files[1], files[4]], 'ios-compile')
assert before == {str(p): sha(p) for p in files}
manifest = {'sources': before, 'commands': commands, 'host_result': result,
            'ios_target': 'arm64-apple-ios17.0', 'strict_swift_version': 6,
            'scope': 'Original Foundation state tests with ThreadSanitizer; UIKit/SwiftUI compile only. No displayed frame or physical attachment validation.',
            'artifacts': {p.name: sha(p) for p in out.iterdir() if p.is_file()}}
(out/'validation-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(result)
print(out/'validation-manifest.json')
