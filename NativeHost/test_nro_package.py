#!/usr/bin/env python3
"""Verify packed dynamic modules with synthetic inputs and the actual Swift checker."""
from pathlib import Path
import argparse, copy, hashlib, json, os, subprocess, sys
import package


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    assert not out.is_relative_to(root) and not out.exists()
    out.mkdir(parents=True)
    package.wait_for_slot(out)
    rewrite = out / 'rewritten'; rewrite.mkdir()
    layout = {'TEXT_END': '0x4000', 'PATCH_OFF': '0x8000', 'KEY_PAGE_OFF': '0x4000', 'KEY_PAGE_SIZE': '0x4000', 'BUILD_ID': '1234'}
    for i, name in enumerate(['main', 'nro0', 'nro1']):
        (rewrite / (name + '.patched.bin')).write_bytes(bytes([i + 1]) * 0x8100)
        (rewrite / (name + '.fixups.tsv')).write_text(''.join(f'# {k} {v}\n' for k, v in layout.items()))
        (rewrite / (name + '.gates.tsv')).write_text('# synthetic gates\n')
    table = ''.join(f'nro{i}\t' + str(i) * 64 + '\t33024\n' for i in range(2)).encode()
    (rewrite / 'nro.tsv').write_bytes(table)
    sha = lambda b: hashlib.sha256(b).hexdigest()
    profile = dict(titleID='010000000000FFFF', version='1', name='Synthetic NRO test', packageName='synthetic',
                   phonePrepare=False, enginePin='synthetic', shaderChain='synthetic', shaderInputs='synthetic',
                   profileSHA256=sha(b'synthetic profile\n'), modules=['main'], buildIDs={'main': '1234'}, environment={},
                   requiredFiles=['game.dylib', 'profile.tsv'], nroInventorySHA256=sha(table), nroBuildIDs={'nro0': '1234', 'nro1': '1234'})
    package.PROFILE = profile; package.MODULES = ['main']
    flags = subprocess.check_output([sys.executable, args.engine / 'dm/ios/tools/gen_embed.py', rewrite, out / 'embed.S', 'main'], text=True).split()
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    dylib = out / 'synthetic.dylib'
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-target', 'arm64-apple-ios26.0', '-isysroot', sdk,
                    '-dynamiclib', *flags, out / 'embed.S', '-o', dylib], check=True)
    sections = package.image_sections(dylib)
    package.compare_sections(dylib, sections, rewrite)
    package.compare_nro_sections(dylib, sections, rewrite)
    count = 1
    def rejected(action):
        nonlocal count
        try: action()
        except (AssertionError, ValueError): count += 1
        else: raise AssertionError('Invalid synthetic NRO container accepted')
    original = dylib.read_bytes()
    for offset in [sections[('__DATA', '__dm_nro_tab')]['offset'],
                   sections[('__DATA', '__dm_nro_tab')]['offset'] + 32,
                   sections[('__TEXT', '__dm_nro_x0')]['offset'],
                   sections[('__DM_NRO_W', '__img')]['offset'],
                   sections[('__TEXT', '__dm_nro_x1')]['offset']]:
        altered = bytearray(original); altered[offset] ^= 1; dylib.write_bytes(altered)
        rejected(lambda: package.compare_nro_sections(dylib, sections, rewrite))
    dylib.write_bytes(original)
    (rewrite / 'nro.tsv').write_bytes(table + b'# altered inventory\n')
    rejected(lambda: package.reviewed_nros(rewrite))
    (rewrite / 'nro.tsv').write_bytes(table)
    package.PROFILE = {k: v for k, v in profile.items() if not k.startswith('nro')}
    rejected(lambda: package.reviewed_nros(rewrite))
    package.PROFILE = profile
    checker = out / 'verify'; checker.mkdir()
    source = (root / 'NativeHost/PreparedGame.swift').read_text().split('\nstruct PreparedGameView: View {')[0].replace('import SwiftUI', 'import Foundation')
    for directory in ['documentDirectory', 'applicationSupportDirectory']:
        source = source.replace(f'fm.urls(for: .{directory}, in: .userDomainMask)[0]', 'URL(fileURLWithPath: CommandLine.arguments[3])')
    source += '''
func SatchelNativeSessionStarted() -> Bool { false }
do {
    let profiles = try JSONDecoder().decode([NativeGameProfile].self, from: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2])))
    _ = try PreparedPackage.verify(root: URL(fileURLWithPath: CommandLine.arguments[1]), profiles: profiles)
    print("PASS")
} catch { print(error.localizedDescription); exit(1) }
'''
    (checker / 'main.swift').write_text(source)
    exe = checker / 'verify'
    subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-O', '-num-threads', '2', root / 'App/NativeGameProfile.swift', root / 'App/PreparedAddOn.swift', checker / 'main.swift', '-o', exe], check=True)
    registry = out / 'profiles.json'; registry.write_text(json.dumps([profile]))
    prepared = out / 'prepared'; prepared.mkdir()
    files = {'game.dylib': original, 'profile.tsv': b'synthetic profile\n', 'dm/nro.tsv': table}
    for name in ['main', 'nro0', 'nro1']:
        for suffix in ['fixups.tsv', 'gates.tsv']:
            files[f'dm/{name}.{suffix}'] = (rewrite / f'{name}.{suffix}').read_bytes()
    for name, data in files.items():
        path = prepared / name; path.parent.mkdir(exist_ok=True); path.write_bytes(data)
    manifest = {k: profile[k] for k in ['titleID', 'version', 'enginePin', 'shaderChain', 'shaderInputs', 'modules', 'environment']}
    manifest.update(format=1, signingStatus='signed', files={n: {'bytes': len(d), 'sha256': sha(d)} for n, d in files.items()})
    def check(value, expected=None):
        nonlocal count
        (prepared / 'manifest.json').write_text(json.dumps(value))
        result = subprocess.run([exe, prepared, registry, checker], capture_output=True, text=True)
        assert (result.returncode == 0 if expected is None else result.returncode == 1 and expected in result.stdout), result.stdout + result.stderr
        count += 1
    check(manifest); check(manifest)
    altered = copy.deepcopy(manifest); del altered['files']['dm/nro0.gates.tsv']; check(altered, 'required binding')
    altered = copy.deepcopy(manifest); altered['files']['dm/nro.tsv']['sha256'] = '0' * 64; check(altered, 'NRO inventory')
    path = prepared / 'dm/nro0.fixups.tsv'; saved = path.read_bytes(); path.write_bytes(saved.replace(b'1234', b'abcd'))
    altered = copy.deepcopy(manifest); altered['files']['dm/nro0.fixups.tsv']['sha256'] = sha(path.read_bytes()); check(altered, 'Module build')
    path.write_bytes(saved)
    registry.write_text(json.dumps([{k: v for k, v in profile.items() if not k.startswith('nro')}]))
    check(manifest, 'does not support NRO')
    print(f'PASS: {count} synthetic NRO container and actual Swift verifier checks')


if __name__ == '__main__': main()
