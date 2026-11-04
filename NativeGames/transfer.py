#!/usr/bin/env python3
"""Print device-owner commands by default. Workers must not use --execute."""
from pathlib import Path
import argparse, json, shlex, subprocess
from registry import profile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    parser.add_argument('--app', type=Path, default=Path.home() / 'SatchelBuild/games/DerivedData/Build/Products/Release-iphoneos/SatchelDev.app')
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    package = args.package.resolve()
    manifest = json.loads((package / 'manifest.json').read_text())
    profile(manifest['titleID'], manifest['version'])
    if manifest.get('signingStatus', 'signed') != 'signed': parser.error('The package needs signature.')
    device = '00008160-001A30E02E600036'
    bundle = 'com.dell.satchel.dev'
    commands = [['xcrun', 'devicectl', 'device', 'install', 'app', '--device', device, str(args.app)]]
    for name in ['game.dylib', 'profile.tsv', 'dm', 'msl', 'romfs.img', 'manifest.json']:
        if not (package / name).exists(): parser.error('Missing package item: ' + name)
        commands.append(['xcrun', 'devicectl', 'device', 'copy', 'to', '--device', device, '--domain-type', 'appDataContainer',
                         '--domain-identifier', bundle, '--source', str(package / name),
                         '--destination', 'Documents/PreparedGames/' + package.name + '/' + name])
    commands.append(['xcrun', 'devicectl', 'device', 'process', 'launch', '--device', device, bundle, '--',
                     '-SatchelAutoLaunchPreparedGame', 'YES', '-SatchelPreparedGame', package.name])
    for cmd in commands:
        print(shlex.join(cmd), flush=True)
        if args.execute: subprocess.run(cmd, check=True)

if __name__ == '__main__': main()
