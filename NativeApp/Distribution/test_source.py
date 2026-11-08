#!/usr/bin/env python3
"""Synthetic archive and source-contract checks. Never uses a signing identity."""
import copy
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
import zipfile

import source


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='satchel-source-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ipa = self.root / 'Satchel.ipa'
        self.info = {
            'CFBundleIdentifier': source.BUNDLE, 'CFBundleDisplayName': 'Satchel',
            'CFBundlePackageType': 'APPL', 'CFBundleSupportedPlatforms': ['iPhoneOS'],
            'CFBundleExecutable': 'Satchel', 'CFBundleShortVersionString': '2.3',
            'CFBundleVersion': '17', 'MinimumOSVersion': '26.0',
            'UISupportedInterfaceOrientations': ['UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
            'NSMicrophoneUsageDescription': 'Synthetic microphone permission.',
            source.METADATA: {'entitlements': ['com.apple.developer.kernel.increased-memory-limit'],
                              'buildDate': '2026-11-02T12:34:56Z'},
        }

    def write_ipa(self, extra=None):
        with zipfile.ZipFile(self.ipa, 'w') as archive:
            archive.writestr('Payload/Satchel.app/Info.plist', plistlib.dumps(self.info))
            archive.writestr('Payload/Satchel.app/Satchel', b'Synthetic executable placeholder')
            for key, value in (extra or {}).items():
                archive.writestr(key, value)

    def test_metadata_and_legacy_match_real_archive_size(self):
        self.write_ipa()
        result = source.make_source(self.ipa, 'https://example.invalid/satchel')
        app = result['apps'][0]
        self.assertEqual(app['versions'][0], {'version': '2.3', 'buildVersion': '17',
            'date': '2026-11-02T12:34:56Z', 'downloadURL': 'https://example.invalid/satchel/Satchel.ipa',
            'size': self.ipa.stat().st_size, 'minOSVersion': '26.0', 'sha256': source.sha256(self.ipa)})
        self.assertEqual(app['appPermissions']['privacy'], {'NSMicrophoneUsageDescription': 'Synthetic microphone permission.'})
        self.assertEqual(app['appPermissions']['entitlements'], ['com.apple.developer.kernel.increased-memory-limit'])
        source.validate_source(json.loads(json.dumps(result)))

    def test_schema_and_tracked_landscape_screenshots(self):
        import schema
        self.write_ipa()
        result = source.make_source(self.ipa, 'https://example.invalid/satchel')
        schema.validate(result)
        shots = result['apps'][0]['screenshots']
        self.assertEqual(len(shots), 4)
        self.assertTrue(all(shot['width'] > shot['height'] for shot in shots))
        self.assertEqual(result['apps'][0]['screenshotURLs'], [shot['imageURL'] for shot in shots])
        with self.assertRaises(ValueError):
            schema.validate(dict(result, unexpected=True))

    def test_framework_privacy_is_included(self):
        self.write_ipa({'Payload/Satchel.app/Frameworks/Test.framework/Info.plist':
            plistlib.dumps({'NSCameraUsageDescription': 'Synthetic camera permission.'})})
        usage = source.make_source(self.ipa, 'https://example.invalid')['apps'][0]['appPermissions']['privacy']
        self.assertEqual(len(usage), 2)

    def test_external_export_requires_explicit_metadata(self):
        del self.info[source.METADATA]
        self.write_ipa()
        with self.assertRaisesRegex(ValueError, 'No exported entitlements'):
            source.make_source(self.ipa, 'https://example.invalid')
        entitlements = self.root / 'built.entitlements'
        entitlements.write_bytes(plistlib.dumps({'keychain-access-groups': ['SYNTHETIC.test']}))
        result = source.make_source(self.ipa, 'https://example.invalid', entitlements, '2026-11-02T00:00:00Z')
        self.assertEqual(result['apps'][0]['appPermissions']['entitlements'], ['keychain-access-groups'])
        self.assertNotIn('SYNTHETIC.test', json.dumps(result))

    def test_rejects_schema_errors(self):
        self.write_ipa()
        valid = source.make_source(self.ipa, 'https://example.invalid')
        edits = [lambda app: app.pop('versions'), lambda app: app.update(bundleIdentifier='com.test'),
                 lambda app: app.update(size=0), lambda app: app['versions'][0].update(size=True),
                 lambda app: app['versions'][0].update(date='yesterday'),
                 lambda app: app['versions'][0].update(date='2026-11-02'),
                 lambda app: app['versions'][0].update(downloadURL='file:///tmp/Satchel.ipa'),
                 lambda app: app['appPermissions'].update(entitlements={}),
                 lambda app: app['appPermissions'].update(privacy=[]),
                 lambda app: app.pop('screenshots'),
                 lambda app: app['screenshots'][0].update(width=0),
                 lambda app: app['versions'][0].update(sha256='invalid'),
                 lambda app: app['appPermissions']['entitlements'].append(1)]
        for edit in edits:
            with self.subTest(edit=edit):
                broken = copy.deepcopy(valid)
                edit(broken['apps'][0])
                with self.assertRaises(ValueError):
                    source.validate_source(broken)

    def test_rejects_unresolved_or_wrong_bundle_metadata(self):
        for field, value in [('CFBundleIdentifier', 'com.dell.satchel.dev'),
                             ('CFBundleVersion', '$(CURRENT_PROJECT_VERSION)'),
                             ('CFBundleSupportedPlatforms', ['iPhoneSimulator']),
                             ('MinimumOSVersion', 'twenty six')]:
            with self.subTest(field=field):
                changed = dict(self.info, **{field: value})
                with self.assertRaises(ValueError):
                    source.info_fields(changed)

    def test_rejects_unsafe_and_unsupported_archives(self):
        for path in ['../escape', '/absolute', 'Payload/Satchel.app/PlugIns/Test.appex/Info.plist']:
            with self.subTest(path=path):
                self.write_ipa({path: b'fixture'})
                with self.assertRaises(ValueError):
                    source.inspect_ipa(self.ipa)

    def test_rejects_missing_executable_and_duplicate_members(self):
        with zipfile.ZipFile(self.ipa, 'w') as archive:
            archive.writestr('Payload/Satchel.app/Info.plist', plistlib.dumps(self.info))
        with self.assertRaisesRegex(ValueError, 'executable missing'):
            source.inspect_ipa(self.ipa)
        self.write_ipa()
        with zipfile.ZipFile(self.ipa, 'a') as archive:
            with self.assertWarns(UserWarning):
                archive.writestr('Payload/Satchel.app/Satchel', b'duplicate')
        with self.assertRaisesRegex(ValueError, 'Duplicate ZIP'):
            source.inspect_ipa(self.ipa)

    def test_rejects_conflicting_or_nested_privacy(self):
        self.write_ipa({'Payload/Satchel.app/Frameworks/Test.framework/Info.plist':
            plistlib.dumps({'NSMicrophoneUsageDescription': 'Different purpose'})})
        with self.assertRaisesRegex(ValueError, 'Conflicting privacy'):
            source.inspect_ipa(self.ipa)
        with self.assertRaisesRegex(ValueError, 'Non-string privacy'):
            source.privacy({'NSLocationTemporaryUsageDescriptionDictionary': {'purpose': 'test'}})

    def test_published_layout_is_templated_by_version(self):
        self.write_ipa()
        result = source.make_source(self.ipa)
        app = result['apps'][0]
        raw = 'https://raw.githubusercontent.com/satchelemu/satchel/'
        self.assertEqual(result['sourceURL'], raw + 'main/source.json')
        self.assertEqual(app['downloadURL'], 'https://github.com/satchelemu/satchel/releases/download/v2.3/Satchel.ipa')
        self.assertEqual(app['versions'][0]['downloadURL'], app['downloadURL'])
        self.assertEqual(app['iconURL'], raw + 'v2.3/App/Assets.xcassets/AppIcon.appiconset/AppIcon.png')
        self.assertEqual(app['screenshotURLs'], [raw + 'v2.3/docs/screenshots/' + name for name in
                                                 ['library.jpg', 'game-detail.jpg', 'import.jpg', 'settings-keys.jpg']])
        self.assertNotIn('example.invalid', json.dumps(result))
        source.validate_source(result)
        # Every URL that points into the repository names a tracked file at the tag.
        for link in [app['iconURL'], *app['screenshotURLs']]:
            self.assertTrue((source.ROOT / link.split('/v2.3/', 1)[1]).is_file(), link)

    def test_layout_rejects_wrong_shapes(self):
        metadata = source.release_metadata()
        edits = [('repository', 'someone else/satchel'), ('tag', 'v{version}/x'), ('tag', '{commit}'),
                 ('download', 'https://example.invalid/{tag}/Satchel.ipa'),
                 ('download', 'http://github.com/{repository}/releases/download/{tag}/Satchel.ipa'),
                 ('download', 'https://github.com/{repository}/releases/latest/download/Satchel.ipa'),
                 ('download', 'https://github.com/other/repo/releases/download/{tag}/Satchel.ipa'),
                 ('source', 'https://raw.githubusercontent.com/{repository}/{sourceBranch}/{secret}'),
                 ('screenshot', 'https://raw.githubusercontent.com/{repository}/{tag}/docs/screenshots'),
                 ('icon', 'https://raw.githubusercontent.com/{repository}/{tag}/{name}'),
                 ('iconPath', 'docs/screenshots/library.jpg'), ('sourcePath', '../source.json'),
                 ('sourceBranch', '/main')]
        for key, value in edits:
            with self.subTest(key=key, value=value):
                changed = copy.deepcopy(metadata)
                changed['publish'][key] = value
                with self.assertRaises((ValueError, KeyError)):
                    source.release_layout('1.0', '1', changed)
        with self.assertRaises(ValueError):
            source.release_layout('1.0', '1', {'version': '1.0'})

    def test_rejects_invalid_urls(self):
        self.write_ipa()
        for value in ['http://example.invalid', 'https://user:secret@example.invalid',
                      'https://example.invalid?query=1', 'https://example.invalid/#fragment']:
            with self.subTest(url=value), self.assertRaises(ValueError):
                source.make_source(self.ipa, value)


if __name__ == '__main__':
    unittest.main(verbosity=2)
