#!/usr/bin/env python3
"""Export an unsigned Release IPA and generate a local SideStore/AltStore source."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import stat
import subprocess
import tempfile
from urllib.parse import urlsplit
import zipfile

BUNDLE = 'com.dell.satchel'
DEFAULT_OUT = Path.home() / 'SatchelBuild/dlc'
METADATA = 'SatchelDistribution'
MACHO = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
         b'\xfe\xed\xfa\xce', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def text(value, label):
    require(isinstance(value, str) and bool(value.strip()) and '$(' not in value,
            label + ' must be a resolved, nonempty string')
    return value


def date(value):
    text(value, 'build date')
    parsed = datetime.fromisoformat(value.replace('Z', '+00:00'))
    require(parsed.tzinfo is not None, 'build date must include a timezone')
    return parsed.isoformat(timespec='seconds').replace('+00:00', 'Z')


def url(value):
    parts = urlsplit(text(value, 'URL'))
    require(parts.scheme == 'https' and bool(parts.hostname) and not parts.username
            and not parts.password and not any(c.isspace() for c in value),
            'URLs must be absolute HTTPS URLs without credentials or whitespace')
    return value


def info_fields(info):
    require(info.get('CFBundleIdentifier') == BUNDLE, 'Expected com.dell.satchel')
    require(info.get('CFBundleDisplayName') == 'Satchel', 'Expected display name Satchel')
    require(info.get('CFBundlePackageType') == 'APPL', 'Expected an application bundle')
    require('iPhoneOS' in info.get('CFBundleSupportedPlatforms', []), 'Expected an iPhoneOS build')
    for key in ['CFBundleShortVersionString', 'CFBundleVersion', 'MinimumOSVersion', 'CFBundleExecutable']:
        text(info.get(key), key)
    require(re.fullmatch(r'\d+(\.\d+){0,2}', info['MinimumOSVersion']), 'Invalid MinimumOSVersion')
    require(Path(info['CFBundleExecutable']).name == info['CFBundleExecutable'], 'Invalid executable name')
    require(set(info.get('UISupportedInterfaceOrientations', [])) == {
        'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'}, 'Expected landscape-only app')


def entitlement_names(path):
    values = plistlib.loads(path.read_bytes())
    require(isinstance(values, dict), 'Entitlements must be a plist dictionary')
    # Values may include account identifiers. Only permission names enter the source.
    return sorted(text(key, 'entitlement name') for key in values)


def privacy(info):
    result = {}
    for key, value in info.items():
        if 'UsageDescription' not in key:
            continue
        require(isinstance(value, str), 'Non-string privacy declarations need explicit source-format support')
        result[key] = text(value, 'privacy description')
    return dict(sorted(result.items()))


def export(build_root, out):
    product = build_root / 'app/DerivedData/Build/Products/Release-iphoneos/Satchel.app'
    info = plistlib.loads((product / 'Info.plist').read_bytes())
    info_fields(info)
    project = json.loads((build_root / 'app/project.json').read_text())
    settings = project['targets']['Satchel']['settings']['base']
    entitlements = build_root / 'app/Satchel.entitlements'
    require(Path(settings['CODE_SIGN_ENTITLEMENTS']).resolve() == entitlements.resolve(),
            'Entitlements must come from this generated build project')
    names = entitlement_names(entitlements)
    require(not list(product.rglob('*.appex')), 'App extensions need their own entitlement export support')
    require(not list(product.rglob('*.app')), 'Nested apps need their own entitlement export support')
    forbidden = {'.p12', '.pfx', '.key', '.pem', '.xci', '.nsp', '.xcz', '.nsz', '.nca'}
    for path in product.rglob('*'):
        require(not path.is_symlink(), 'Symlinks are not supported in this iOS export')
        require(path.suffix.lower() not in forbidden and path.name not in {
            'prod.keys', 'title.keys', 'romfs.img', 'game.dylib', 'b-e65a4ebd3322'}
            and not path.name.endswith('.patched.bin'), 'Private input found in build; refusing export')
    out.mkdir(parents=True, exist_ok=True)
    require(shutil.disk_usage(out).free >= 36 * 1024**3, 'Disk reserve: need 36 GiB before export')
    destination = out / 'Satchel.ipa'
    require(not destination.exists(), 'Satchel.ipa already exists; choose a fresh output directory')
    built = datetime.fromtimestamp((product / info['CFBundleExecutable']).stat().st_mtime, timezone.utc)
    with tempfile.TemporaryDirectory(prefix='distribution-', dir=out) as temporary:
        stage = Path(temporary)
        app = stage / 'Payload/Satchel.app'
        shutil.copytree(product, app)
        all_names = set(names)
        for path in app.rglob('*'):
            if not path.is_file():
                continue
            with path.open('rb') as stream:
                magic = stream.read(4)
            if magic not in MACHO:
                continue
            # Public entitlement metadata only. Never print codesign's identity output.
            existing = subprocess.run(['codesign', '-d', '--entitlements', '-', '--xml', str(path)], capture_output=True)
            if existing.returncode == 0 and existing.stdout.strip():
                all_names.update(plistlib.loads(existing.stdout).keys())
            signed = subprocess.run(['codesign', '-d', str(path)], capture_output=True)
            if signed.returncode == 0:
                removed = subprocess.run(['codesign', '--remove-signature', str(path)], capture_output=True)
                require(removed.returncode == 0, 'Could not remove a staged binary signature')
            unsigned = subprocess.run(['codesign', '-d', str(path)], capture_output=True)
            require(unsigned.returncode != 0 and b'not signed at all' in unsigned.stderr,
                    'Could not confirm a staged binary is unsigned')
        for path in list(app.rglob('embedded.mobileprovision')):
            path.unlink()
        for path in list(app.rglob('_CodeSignature')):
            shutil.rmtree(path)
        info[METADATA] = {'entitlements': sorted(all_names), 'buildDate': date(built.isoformat())}
        (app / 'Info.plist').write_bytes(plistlib.dumps(info))
        archive = stage / 'Satchel.ipa'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
            for path in sorted((stage / 'Payload').rglob('*')):
                if path.is_file():
                    require(shutil.disk_usage(out).free >= 35 * 1024**3, 'Disk reserve: export stopped at 35 GiB')
                    zipped.write(path, path.relative_to(stage))
        with zipfile.ZipFile(archive) as zipped:
            require(zipped.testzip() is None, 'IPA CRC verification failed')
        os.rename(archive, destination)
    print('PASS: exported unsigned Satchel.ipa; no signing identity used')
    return destination


def inspect_ipa(path, entitlements=None, build_date=None):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(set(names)) == len(names), 'Duplicate ZIP members')
        for member in archive.infolist():
            parts = PurePosixPath(member.filename)
            require(not parts.is_absolute() and '..' not in parts.parts and '\\' not in member.filename,
                    'Unsafe ZIP path')
            require(not stat.S_ISLNK(member.external_attr >> 16), 'ZIP symlinks are unsupported')
        roots = [name for name in names if re.fullmatch(r'Payload/[^/]+\.app/Info\.plist', name)]
        require(len(roots) == 1, 'Expected exactly one app in Payload')
        require(not any('.appex/' in name or name.count('.app/') > 1 for name in names),
                'Extensions and nested apps require separate permission collection')
        member = archive.getinfo(roots[0])
        require(member.file_size <= 4 * 1024**2, 'Info.plist exceeds metadata limit')
        info = plistlib.loads(archive.read(member))
        info_fields(info)
        require(roots[0].removesuffix('Info.plist') + info['CFBundleExecutable'] in names,
                'App executable missing from IPA')
        metadata = info.get(METADATA, {})
        permissions = entitlement_names(entitlements) if entitlements else metadata.get('entitlements')
        require(isinstance(permissions, list) and all(isinstance(key, str) and key for key in permissions),
                'No exported entitlements; supply the built entitlements plist')
        built = date(build_date or metadata.get('buildDate'))
        # Privacy strings must come from every built bundle, including embedded frameworks.
        usage = privacy(info)
        for name in names:
            if name != roots[0] and name.endswith('/Info.plist'):
                require(archive.getinfo(name).file_size <= 4 * 1024**2, 'Nested plist exceeds metadata limit')
                for key, value in privacy(plistlib.loads(archive.read(name))).items():
                    require(key not in usage or usage[key] == value, 'Conflicting privacy descriptions between bundles')
                    usage[key] = value
    return info, sorted(set(permissions)), dict(sorted(usage.items())), built


def validate_source(source):
    """Validate the emitted subset of the documented Classic/SideStore source schema.

    Reference: https://faq.altstore.io/developers/make-a-source (2026-11-02).
    Legacy version fields are emitted alongside versions for older SideStore clients.
    This validates structure and values, not network availability or signing support.
    """
    require(isinstance(source, dict), 'Source must be an object')
    for key in ['name', 'identifier']:
        text(source.get(key), key)
    url(source.get('sourceURL'))
    require(source.get('news') == [], 'Expected an empty news list')
    apps = source.get('apps')
    require(isinstance(apps, list) and len(apps) == 1, 'Expected one app')
    app = apps[0]
    require(isinstance(app, dict) and app.get('name') == 'Satchel' and app.get('bundleIdentifier') == BUNDLE,
            'Invalid Satchel app identity')
    for key in ['developerName', 'localizedDescription']:
        text(app.get(key), key)
    url(app.get('iconURL'))
    require(app.get('screenshots') == [] and app.get('screenshotURLs') == [],
            'Expected empty screenshots placeholders')
    versions = app.get('versions')
    require(isinstance(versions, list) and len(versions) == 1 and isinstance(versions[0], dict),
            'Expected one version object')
    version = versions[0]
    for key in ['version', 'buildVersion']:
        text(version.get(key), key)
    require(re.fullmatch(r'\d+(\.\d+){0,2}', text(version.get('minOSVersion'), 'minOSVersion')),
            'Invalid minimum OS version')
    date(version.get('date'))
    url(version.get('downloadURL'))
    require(type(version.get('size')) is int and version['size'] > 0, 'Size must be positive integer bytes')
    for old, new in [('version', 'version'), ('versionDate', 'date'), ('downloadURL', 'downloadURL'),
                     ('size', 'size'), ('minOSVersion', 'minOSVersion')]:
        require(app.get(old) == version[new], 'Legacy version metadata does not match versions')
    permissions = app.get('appPermissions')
    require(isinstance(permissions, dict), 'appPermissions must be an object')
    entitlements = permissions.get('entitlements')
    require(isinstance(entitlements, list), 'entitlements must be an array')
    for name in entitlements:
        text(name, 'entitlement')
    require(len(entitlements) == len(set(entitlements)), 'Duplicate entitlement')
    usage = permissions.get('privacy')
    require(isinstance(usage, dict), 'privacy must be a dictionary')
    for key, value in usage.items():
        text(key, 'privacy key')
        text(value, 'privacy description')


def make_source(ipa, base_url, entitlements=None, build_date=None):
    info, permissions, usage, built = inspect_ipa(ipa, entitlements, build_date)
    base = url(base_url).rstrip('/')
    require(not urlsplit(base).query and not urlsplit(base).fragment, 'Base URL must not have a query or fragment')
    version = {'version': info['CFBundleShortVersionString'], 'buildVersion': info['CFBundleVersion'],
               'date': built, 'downloadURL': base + '/Satchel.ipa', 'size': ipa.stat().st_size,
               'minOSVersion': info['MinimumOSVersion']}
    app = {'name': 'Satchel', 'bundleIdentifier': BUNDLE, 'developerName': 'Satchel contributors',
           'localizedDescription': 'Prepare and play your own Switch dumps on iPhone. '
               'Personal signing required. New-account game signing is not yet supported; see the install guide.',
           'iconURL': base + '/icon.png', 'screenshots': [], 'screenshotURLs': [], 'versions': [version],
           'appPermissions': {'entitlements': permissions, 'privacy': usage}}
    for old, new in [('version', 'version'), ('versionDate', 'date'), ('downloadURL', 'downloadURL'),
                     ('size', 'size'), ('minOSVersion', 'minOSVersion')]:
        app[old] = version[new]
    source = {'name': 'Satchel', 'identifier': 'com.dell.satchel.source', 'sourceURL': base + '/source.json',
              'apps': [app], 'news': []}
    validate_source(source)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    exporter = commands.add_parser('export', help='Copy a canonical Release build into an unsigned IPA')
    exporter.add_argument('--build-root', type=Path, required=True)
    exporter.add_argument('--out', type=Path, default=DEFAULT_OUT)
    generator = commands.add_parser('generate', help='Generate and validate source.json from an exported IPA')
    generator.add_argument('ipa', type=Path)
    generator.add_argument('--out', type=Path, default=DEFAULT_OUT)
    generator.add_argument('--base-url', default='https://example.invalid/satchel')
    generator.add_argument('--entitlements', type=Path, help='Built entitlements plist for an externally exported IPA')
    generator.add_argument('--build-date', help='ISO 8601 build timestamp with timezone for an external export')
    checker = commands.add_parser('validate', help='Validate source schema and compare every field with the IPA')
    checker.add_argument('source', type=Path)
    checker.add_argument('ipa', type=Path)
    checker.add_argument('--entitlements', type=Path)
    checker.add_argument('--build-date')
    args = parser.parse_args()
    try:
        if args.command == 'export':
            export(args.build_root.expanduser().resolve(), args.out.expanduser().resolve())
        elif args.command == 'generate':
            source = make_source(args.ipa, args.base_url, args.entitlements, args.build_date)
            args.out.mkdir(parents=True, exist_ok=True)
            destination = args.out / 'source.json'
            with destination.open('x') as stream:
                stream.write(json.dumps(source, indent=2, ensure_ascii=False) + '\n')
            print('PASS: source schema and IPA fields validated; URLs may be placeholders')
        else:
            source = json.loads(args.source.read_text())
            validate_source(source)
            base = source['sourceURL'].removesuffix('/source.json')
            expected = make_source(args.ipa, base, args.entitlements, args.build_date)
            require(source == expected, 'Source fields do not match the IPA and configured URLs')
            digest = hashlib.sha256()
            with args.ipa.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            print('PASS: source schema and all fields match IPA; SHA-256 ' + digest.hexdigest())
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile, plistlib.InvalidFileException) as error:
        parser.exit(1, 'ERROR: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
