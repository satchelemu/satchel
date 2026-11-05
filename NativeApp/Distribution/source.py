#!/usr/bin/env python3
"""Export an unsigned Release IPA and generate a local SideStore/AltStore source."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
from urllib.parse import urlsplit
import zipfile

BUNDLE = 'com.dell.satchel'
DEFAULT_OUT = Path.home() / 'SatchelBuild/distribution'
METADATA = 'SatchelDistribution'
ROOT = Path(__file__).resolve().parents[2]


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
    # Keep the original command compatible; the canonical packager owns the contract.
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
    from build_ipa import export as build_ipa
    return build_ipa(build_root, out)


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
    from schema import validate
    validate(source)
    require(app['screenshotURLs'] == [item['imageURL'] for item in app['screenshots']],
            'Legacy screenshots must match screenshots')
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


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def jpeg_size(path):
    # Read marker headers only. No image library or re-encoding of tracked screenshots.
    import struct
    with path.open('rb') as stream:
        require(stream.read(2) == b'\xff\xd8', 'Expected a JPEG screenshot')
        while True:
            marker = stream.read(2)
            require(len(marker) == 2 and marker[0] == 255, 'Invalid JPEG marker')
            while marker[1] == 255:
                marker = b'\xff' + stream.read(1)
            require(marker[1] not in {0xda, 0xd9}, 'JPEG has no size marker')
            length = struct.unpack('>H', stream.read(2))[0]
            require(length >= 2, 'Invalid JPEG segment')
            if marker[1] in {0xc0, 0xc1, 0xc2}:
                _, height, width = struct.unpack('>BHH', stream.read(5))
                return width, height
            stream.seek(length - 2, 1)


def screenshots(base):
    metadata = json.loads((ROOT / 'NativeApp/Distribution/release.json').read_text())
    result = []
    for name in metadata['screenshots']:
        require(Path(name).name == name, 'Invalid tracked screenshot name')
        width, height = jpeg_size(ROOT / 'docs/screenshots' / name)
        result.append({'imageURL': base + '/screenshots/' + name, 'width': width, 'height': height})
    return result


def make_source(ipa, base_url, entitlements=None, build_date=None):
    info, permissions, usage, built = inspect_ipa(ipa, entitlements, build_date)
    base = url(base_url).rstrip('/')
    require(not urlsplit(base).query and not urlsplit(base).fragment, 'Base URL must not have a query or fragment')
    version = {'version': info['CFBundleShortVersionString'], 'buildVersion': info['CFBundleVersion'],
               'date': built, 'downloadURL': base + '/Satchel.ipa', 'size': ipa.stat().st_size,
               'minOSVersion': info['MinimumOSVersion'], 'sha256': sha256(ipa)}
    app = {'name': 'Satchel', 'bundleIdentifier': BUNDLE, 'developerName': 'Satchel contributors',
           'localizedDescription': 'Prepare and play your own Switch dumps on iPhone. '
               'Personal signing required. Other accounts and installer capability preservation still need verification; see the install guide.',
           'iconURL': base + '/icon.png', 'screenshots': screenshots(base),
           'screenshotURLs': [item['imageURL'] for item in screenshots(base)], 'versions': [version],
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
