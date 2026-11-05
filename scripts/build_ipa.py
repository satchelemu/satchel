#!/usr/bin/env python3
"""Package a canonical unsigned Release app, without signing identities or uploads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'NativeApp/Distribution'))
from source import info_fields, require, METADATA

MACHO = {bytes.fromhex(value) for value in ['cffaedfe', 'cefaedfe', 'feedfacf', 'feedface',
                                          'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca']}
MEMORY = {'com.apple.developer.kernel.increased-memory-limit',
          'com.apple.developer.kernel.extended-virtual-addressing'}
MANIFEST = 'Satchel.manifest.json'
ENTITLEMENTS = 'Satchel.entitlements'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def reserve(path, extra=0):
    require(shutil.disk_usage(path).free >= 35 * 1024**3 + extra,
            'Disk reserve: packaging requires 35 GiB plus staging space')


def macho(path):
    with path.open('rb') as stream:
        return stream.read(4) in MACHO


def codesign(*args):
    # Capture all output: signed inputs can contain private identity metadata.
    return subprocess.run(['/usr/bin/codesign', *map(str, args)], capture_output=True)


def unsigned(path):
    result = codesign('-d', path)
    return result.returncode != 0 and b'not signed at all' in result.stderr


def safe_path(path):
    require(not path.is_symlink(), 'Symlinks are not supported in an iOS export')
    require(path.is_file() or path.is_dir(), 'Special files are not supported')
    fixture = ROOT / 'App/Resources/hello-horizon.nro'
    if path.is_file() and path.name == fixture.name and digest(path) == digest(fixture):
        return  # Repository-authored synthetic homebrew, never an imported game.
    require(path.suffix.lower() not in {'.p12', '.pfx', '.key', '.keys', '.pem', '.cer', '.crt',
            '.xci', '.nsp', '.xcz', '.nsz', '.nca', '.ncz', '.nso', '.nro', '.romfs', '.spv', '.xcent'}
            and path.name.lower() not in {'romfs.img', 'game.dylib', 'preparedgames', 'saves', 'b-e65a4ebd3322'}
            and not path.name.endswith('.patched.bin'), 'Private input found in build; refusing export')


def requested_entitlements(path):
    values = plistlib.loads(path.read_bytes())
    require(isinstance(values, dict) and all(isinstance(key, str) for key in values), 'Invalid entitlements plist')
    require(all(values.get(key) is True for key in MEMORY), 'Both memory entitlements must be requested')
    require(set(values) == MEMORY | {'com.apple.security.application-groups', 'keychain-access-groups'},
            'Unexpected entitlement keys: review distribution policy')
    require(values['com.apple.security.application-groups'] == ['group.com.dell.satchel.migration']
            and values['keychain-access-groups'] == ['$(AppIdentifierPrefix)com.dell.satchel',
                                                   '$(AppIdentifierPrefix)com.dell.satchel.migration'],
            'Expected account-neutral App Group and Keychain templates')
    return values


def export(build_root, out):
    build_root, out = build_root.resolve(), out.resolve()
    product = build_root / 'app/DerivedData/Build/Products/Release-iphoneos/Satchel.app'
    require(not out.is_relative_to(ROOT) and not out.is_relative_to(product), 'Output must be outside the checkout and app')
    require(not product.is_symlink(), 'App bundle must not be a symlink')
    info = plistlib.loads((product / 'Info.plist').read_bytes())
    info_fields(info)
    metadata = json.loads((ROOT / 'NativeApp/Distribution/release.json').read_text())
    for key, field in [('version', 'CFBundleShortVersionString'), ('build', 'CFBundleVersion'), ('minimumOS', 'MinimumOSVersion')]:
        require(info[field] == metadata[key], 'Build differs from tracked release metadata: ' + field)
    project = json.loads((build_root / 'app/project.json').read_text())
    settings = project['targets']['Satchel']['settings']['base']
    entitlements = build_root / 'app/Satchel.entitlements'
    require(Path(settings['CODE_SIGN_ENTITLEMENTS']).resolve() == entitlements.resolve(),
            'Entitlements must come from this generated build project')
    require(not settings.get('DEVELOPMENT_TEAM'), 'Use an unsigned build without a development team')
    values = requested_entitlements(entitlements)
    paths = sorted(product.rglob('*'))
    for path in paths:
        safe_path(path)
        require(path.suffix not in {'.app', '.appex'}, 'Nested apps and extensions need separate signing support')
    size = sum(path.stat().st_size for path in paths if path.is_file())
    out.mkdir(parents=True, exist_ok=True)
    reserve(out, size * 2 + 16 * 1024**2)
    for name in ['Satchel.ipa', MANIFEST, ENTITLEMENTS]:
        require(not (out / name).exists(), 'Output already exists; choose a fresh output directory')
    with tempfile.TemporaryDirectory(prefix='ipa-', dir=out) as temporary:
        stage = Path(temporary)
        app = stage / 'Payload/Satchel.app'
        shutil.copytree(product, app)
        for path in sorted(app.rglob('*')):
            if not path.is_file() or not macho(path):
                continue
            if codesign('-d', path).returncode == 0:
                require(codesign('--remove-signature', path).returncode == 0, 'Could not remove a staged signature')
            require(unsigned(path), 'Could not confirm a staged binary is unsigned')
        require(macho(app / info['CFBundleExecutable']), 'App executable must be Mach-O')
        for path in app.rglob('embedded.mobileprovision'):
            path.unlink()
        for path in list(app.rglob('_CodeSignature')):
            shutil.rmtree(path)
        # Sideloaders do not expand arbitrary Info.plist build variables. Use the installed
        # signature's default Keychain group and the existing profile-based team fallback.
        info.pop('SatchelPrivateKeychainGroup', None)
        for key in ['ApplicationIdentifier', 'TeamIdentifier', 'AppIdentifierPrefix', 'com.apple.developer.team-identifier']:
            info.pop(key, None)
        info[METADATA] = {'entitlements': sorted(values), 'buildDate': metadata['date']}
        (app / 'Info.plist').write_bytes(plistlib.dumps(info, sort_keys=True))
        # This resource is a template, not a signature. Stores must preserve these capabilities.
        (app / 'SatchelDistribution.entitlements').write_bytes(plistlib.dumps(values, sort_keys=True))
        members = []
        archive = stage / 'Satchel.ipa'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
            for path in sorted([stage / 'Payload', *(stage / 'Payload').rglob('*')],
                               key=lambda p: p.relative_to(stage).as_posix() + ('/' if p.is_dir() else '')):
                reserve(out)
                directory = path.is_dir()
                mode = 0o755 if directory or macho(path) else 0o644
                name = path.relative_to(stage).as_posix() + ('/' if directory else '')
                member = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                member.create_system = 3
                member.external_attr = ((stat.S_IFDIR if directory else stat.S_IFREG) | mode) << 16
                member.compress_type = zipfile.ZIP_DEFLATED
                if directory:
                    member.external_attr |= 0x10
                    zipped.writestr(member, b'')
                else:
                    with path.open('rb') as src, zipped.open(member, 'w') as dst:
                        shutil.copyfileobj(src, dst, length=1024 * 1024)
                    members.append({'path': name, 'size': path.stat().st_size,
                                    'mode': format(mode, '04o'), 'sha256': digest(path)})
        with zipfile.ZipFile(archive) as zipped:
            require(zipped.testzip() is None, 'IPA CRC verification failed')
        manifest = {'formatVersion': 1, 'ipa': {'file': 'Satchel.ipa', 'size': archive.stat().st_size,
                    'sha256': digest(archive)}, 'version': info['CFBundleShortVersionString'],
                    'build': info['CFBundleVersion'], 'minimumOS': info['MinimumOSVersion'],
                    'date': metadata['date'], 'entitlements': sorted(values), 'files': members}
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        (stage / ENTITLEMENTS).write_bytes(plistlib.dumps(values, sort_keys=True))
        for name in ['Satchel.ipa', MANIFEST, ENTITLEMENTS]:
            os.rename(stage / name, out / name)
    print('PASS: deterministic unsigned IPA, hash manifest and account-neutral entitlement template')
    return out / 'Satchel.ipa'


def verify_resign(ipa, manifest_path):
    """Verify hashes, then sign a disposable copy using only the ad hoc identity."""
    manifest = json.loads(manifest_path.read_text())
    require(manifest['ipa']['sha256'] == digest(ipa) and manifest['ipa']['size'] == ipa.stat().st_size,
            'IPA hash or size differs from manifest')
    with zipfile.ZipFile(ipa) as archive:
        members = archive.infolist()
        require(len({m.filename for m in members}) == len(members), 'Duplicate ZIP member')
        reserve(ipa.parent, sum(m.file_size for m in members) + 16 * 1024**2)
        with tempfile.TemporaryDirectory(prefix='resign-', dir=ipa.parent) as temporary:
            stage = Path(temporary)
            expected = {row['path']: row for row in manifest['files']}
            require(len(expected) == len(manifest['files']), 'Duplicate manifest member')
            require(set(expected) == {m.filename for m in members if not m.is_dir()}, 'Manifest member mismatch')
            for member in members:
                name = member.filename
                require((name in {'Payload/', 'Payload/Satchel.app/'} or name.startswith('Payload/Satchel.app/'))
                        and '..' not in Path(name).parts and '\\' not in name
                        and name == Path(name).as_posix() + ('/' if member.is_dir() else ''),
                        'Unsafe archive path or unexpected bundle structure')
                require('.appex/' not in name and name.count('.app/') <= 1,
                        'Extensions and nested apps require separate signing support')
                mode = member.external_attr >> 16
                require(stat.S_ISDIR(mode) if member.is_dir() else stat.S_ISREG(mode), 'Unsafe archive file type')
                require(member.date_time == (1980, 1, 1, 0, 0, 0), 'Noncanonical ZIP timestamp')
                target = stage / name
                if member.is_dir():
                    require(stat.S_IMODE(mode) == 0o755, 'Noncanonical directory permissions')
                    target.mkdir(parents=True, exist_ok=True)
                    target.chmod(0o755)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as src, target.open('wb') as dst:
                    shutil.copyfileobj(src, dst, length=1024 * 1024)
                row = expected[name]
                require(digest(target) == row['sha256'] and target.stat().st_size == row['size']
                        and format(stat.S_IMODE(mode), '04o') == row['mode'], 'Manifest content or permission mismatch')
                require(stat.S_IMODE(mode) == (0o755 if macho(target) else 0o644), 'Noncanonical file permissions')
                target.chmod(stat.S_IMODE(mode))
            app = stage / 'Payload/Satchel.app'
            info = plistlib.loads((app / 'Info.plist').read_bytes())
            info_fields(info)
            for path in app.rglob('*'):
                safe_path(path)
            require(not list(app.rglob('embedded.mobileprovision')) and not list(app.rglob('_CodeSignature')),
                    'Unsigned IPA contains signing artifacts')
            template = app / 'SatchelDistribution.entitlements'
            values = requested_entitlements(template)
            require(sorted(values) == manifest['entitlements'] == info[METADATA]['entitlements'],
                    'Entitlement declarations differ')
            # Only for the disposable ad hoc check. A real installer supplies its own prefix.
            adhoc = {key: value for key, value in values.items()}
            adhoc['keychain-access-groups'] = [value.replace('$(AppIdentifierPrefix)', '')
                                              for value in values['keychain-access-groups']]
            sign_plist = stage / 'adhoc.entitlements'
            sign_plist.write_bytes(plistlib.dumps(adhoc))
            binaries = [p for p in app.rglob('*') if p.is_file() and macho(p)]
            require(binaries and all(unsigned(p) for p in binaries), 'IPA must have only unsigned Mach-O code')
            for path in sorted(binaries, key=lambda p: len(p.parts), reverse=True):
                if path == app / info['CFBundleExecutable']:
                    continue
                require(codesign('--force', '--sign', '-', '--timestamp=none', path).returncode == 0,
                        'Ad hoc nested-code signing failed')
            require(codesign('--force', '--sign', '-', '--timestamp=none', '--entitlements', sign_plist,
                             app).returncode == 0, 'Ad hoc app signing failed')
            require(codesign('--verify', '--deep', '--strict', app).returncode == 0, 'Ad hoc signature verification failed')
            actual = codesign('-d', '--entitlements', '-', '--xml', app)
            require(actual.returncode == 0 and plistlib.loads(actual.stdout) == adhoc,
                    'Re-signing did not preserve all requested entitlements')
            require(all(plistlib.loads(actual.stdout).get(key) is True for key in MEMORY),
                    'Re-signed app lost a memory entitlement')
    require(digest(ipa) == manifest['ipa']['sha256'], 'Original IPA changed during re-sign check')
    print('PASS: ad hoc re-sign, strict nested-code verification and all entitlement values; IPA unchanged')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--verify-resign', type=Path, metavar='IPA')
    args = parser.parse_args()
    try:
        if args.verify_resign:
            require(not args.build_root and not args.out, 'Choose export or re-sign verification')
            verify_resign(args.verify_resign, args.verify_resign.parent / MANIFEST)
        else:
            require(args.build_root and args.out, 'Export needs --build-root and --out')
            export(args.build_root, args.out)
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile, plistlib.InvalidFileException) as error:
        parser.exit(1, 'ERROR: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
