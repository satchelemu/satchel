#!/usr/bin/env python3
"""Run the app's Swift package verifier on the Mac, without UIKit or a device."""
from pathlib import Path
import argparse, subprocess, shutil

def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path, nargs='?', default=Path.home() / 'SatchelBuild/host/package/TOTK-1.4.3')
    parser.add_argument('--out-root', type=Path, default=Path.home() / 'SatchelBuild/games/verify')
    parser.add_argument('--expect-unsigned', action='store_true')
    args = parser.parse_args()
    out = args.out_root
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / 'App/NativeRuntimeProfiles.json', out / 'NativeRuntimeProfiles.json')
    source = (root / 'NativeHost/PreparedGame.swift').read_text().split('\n/// The background time around the package check')[0].split('\nstruct PreparedGameView: View {')[0]
    source = source.replace('import SwiftUI', 'import Foundation')
    # Keep diagnostic files and verification stamps inside the task's output directory.
    for directory in ['documentDirectory', 'applicationSupportDirectory']:
        for manager in ['fm', 'FileManager.default']:
            source = source.replace(f'{manager}.urls(for: .{directory}, in: .userDomainMask)[0]',
                                    'URL(fileURLWithPath: CommandLine.arguments[3])')
    source += '''
func SatchelNativeSessionStarted() -> Bool { false }
do {
    let profiles = try JSONDecoder().decode([NativeGameProfile].self, from: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2])))
    _ = try PreparedPackage.verify(root: URL(fileURLWithPath: CommandLine.arguments[1]), profiles: profiles)
    print("PASS: actual host Swift verifier accepted package")
} catch { print("FAIL: \\(error.localizedDescription)"); exit(1) }
'''
    harness = out / 'main.swift'
    harness.write_text(source)
    exe = out / 'verify-prepared-package'
    subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-O', '-num-threads', '2',
                    root / 'App/NativeGameProfile.swift', root / 'App/PreparedAddOn.swift', harness, '-o', exe], check=True)
    result = subprocess.run([exe, args.package, root / 'App/NativeGameProfiles.json', out], text=True, capture_output=True)
    print(result.stdout, end='')
    if args.expect_unsigned:
        assert result.returncode == 1 and 'needs signature' in result.stdout
        print('PASS: unsigned package refused before launch')
    else:
        assert result.returncode == 0, 'Host package verification failed: ' + result.stderr

if __name__ == '__main__': main()
