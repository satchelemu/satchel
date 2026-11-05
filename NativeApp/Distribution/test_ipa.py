#!/usr/bin/env python3
"""Synthetic packaging boundaries; real codesign is checked separately on the app."""
import contextlib
import copy
import io
import json
import os
from pathlib import Path
import plistlib
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import build_ipa


class IPATests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='satchel-ipa-',
            dir=os.environ.get('SATCHEL_DISTRIBUTION_TEST_OUT'))
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.app = self.build / 'app/DerivedData/Build/Products/Release-iphoneos/Satchel.app'
        self.app.mkdir(parents=True)
        release = json.loads((build_ipa.ROOT / 'NativeApp/Distribution/release.json').read_text())
        info = {'CFBundleIdentifier': 'com.dell.satchel', 'CFBundleDisplayName': 'Satchel',
                'CFBundleExecutable': 'Satchel', 'CFBundlePackageType': 'APPL',
                'CFBundleSupportedPlatforms': ['iPhoneOS'], 'CFBundleShortVersionString': release['version'],
                'CFBundleVersion': release['build'], 'MinimumOSVersion': release['minimumOS'],
                'UISupportedInterfaceOrientations': ['UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight']}
        info['SatchelPrivateKeychainGroup'] = '$(AppIdentifierPrefix)com.dell.satchel'
        (self.app / 'Info.plist').write_bytes(plistlib.dumps(info))
        # Deliberately not valid code. Mock only codesign, exercise the real archive writer.
        (self.app / 'Satchel').write_bytes(bytes.fromhex('cffaedfe') + b'synthetic fixture')
        (self.app / 'resource.txt').write_text('Synthetic public resource\n')
        self.entitlements = self.build / 'app/Satchel.entitlements'
        values = {key: True for key in build_ipa.MEMORY}
        values.update({'com.apple.security.application-groups': ['group.com.dell.satchel.migration'],
                       'keychain-access-groups': ['$(AppIdentifierPrefix)com.dell.satchel',
                                                  '$(AppIdentifierPrefix)com.dell.satchel.migration']})
        self.entitlements.write_bytes(plistlib.dumps(values))
        project = {'targets': {'Satchel': {'settings': {'base': {'CODE_SIGN_ENTITLEMENTS': str(self.entitlements)}}}}}
        (self.build / 'app/project.json').write_text(json.dumps(project))
        self.addCleanup(patch.stopall)
        patch.object(build_ipa, 'codesign', return_value=build_ipa.subprocess.CompletedProcess([], 1, b'', b'code object is not signed at all')).start()
        patch.object(build_ipa, 'reserve').start()

    def export(self, name):
        with contextlib.redirect_stdout(io.StringIO()):
            return build_ipa.export(self.build, self.root / name)

    def test_reproducible_modes_manifest_and_unchanged_input(self):
        (self.app / 'a').mkdir()
        (self.app / 'a.txt').write_text('Synthetic ordering edge')
        before = {p.name: p.read_bytes() for p in self.app.iterdir() if p.is_file()}
        first = self.export('first')
        for path in self.app.iterdir():
            os.utime(path, (1800000001, 1800000001))
            path.chmod(0o777)
        second = self.export('second')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual((first.parent / build_ipa.MANIFEST).read_bytes(), (second.parent / build_ipa.MANIFEST).read_bytes())
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.app.iterdir() if p.is_file()})
        with zipfile.ZipFile(first) as archive:
            self.assertEqual(archive.namelist(), sorted(archive.namelist()))
            self.assertNotIn('SatchelPrivateKeychainGroup', plistlib.loads(archive.read('Payload/Satchel.app/Info.plist')))
            for member in archive.infolist():
                self.assertEqual(member.date_time, (1980, 1, 1, 0, 0, 0))
                expected = 0o755 if member.is_dir() or member.filename.endswith('/Satchel') else 0o644
                self.assertEqual(stat.S_IMODE(member.external_attr >> 16), expected)
            manifest = json.loads((first.parent / build_ipa.MANIFEST).read_text())
            self.assertEqual(manifest['ipa']['sha256'], build_ipa.digest(first))
            for row in manifest['files']:
                self.assertEqual(build_ipa.hashlib.sha256(archive.read(row['path'])).hexdigest(), row['sha256'])

    def test_rejects_private_inputs_and_symlinks(self):
        for name in ['prod.keys', 'sample.p12', 'sample.ncz', 'sample.xci', 'hello-horizon.nro', 'game.dylib', 'romfs.img', 'sample.spv']:
            with self.subTest(name=name):
                path = self.app / name
                path.write_bytes(b'synthetic')
                with self.assertRaisesRegex(ValueError, 'Private input'):
                    self.export('blocked')
                path.unlink()
        link = self.app / 'link'
        link.symlink_to(self.app / 'resource.txt')
        with self.assertRaisesRegex(ValueError, 'Symlinks'):
            self.export('blocked')

    def test_removes_profile_and_resource_signature(self):
        (self.app / 'embedded.mobileprovision').write_bytes(b'synthetic profile')
        signature = self.app / '_CodeSignature'
        signature.mkdir()
        (signature / 'CodeResources').write_bytes(b'synthetic signature')
        ipa = self.export('stripped')
        with zipfile.ZipFile(ipa) as archive:
            self.assertFalse(any('mobileprovision' in p or '_CodeSignature' in p for p in archive.namelist()))
        self.assertTrue(signature.exists())

    def test_only_exact_tracked_homebrew_is_allowed(self):
        fixture = build_ipa.ROOT / 'App/Resources/hello-horizon.nro'
        (self.app / fixture.name).write_bytes(fixture.read_bytes())
        self.export('fixture')

    def test_missing_memory_entitlement_fails_closed(self):
        values = plistlib.loads(self.entitlements.read_bytes())
        values.pop(sorted(build_ipa.MEMORY)[0])
        self.entitlements.write_bytes(plistlib.dumps(values))
        with self.assertRaisesRegex(ValueError, 'Both memory'):
            self.export('blocked')

    def test_resolved_keychain_group_rejected(self):
        values = plistlib.loads(self.entitlements.read_bytes())
        values['keychain-access-groups'] = ['not-an-account-template']
        self.entitlements.write_bytes(plistlib.dumps(values))
        with self.assertRaisesRegex(ValueError, 'account-neutral'):
            self.export('blocked')

    def test_version_drift_and_nested_app_rejected(self):
        path = self.app / 'Info.plist'
        info = plistlib.loads(path.read_bytes())
        wrong = copy.deepcopy(info)
        wrong['CFBundleVersion'] = '99999'
        path.write_bytes(plistlib.dumps(wrong))
        with self.assertRaisesRegex(ValueError, 'tracked release metadata'):
            self.export('blocked')
        path.write_bytes(plistlib.dumps(info))
        (self.app / 'Nested.app').mkdir()
        with self.assertRaisesRegex(ValueError, 'Nested apps'):
            self.export('blocked')

    def test_resign_rejects_extra_bundle_and_noncanonical_paths(self):
        for index, name in enumerate(['Payload/Extra.app/', 'Payload//Satchel.app/', 'Payload/Satchel.app/./']):
            with self.subTest(name=name):
                ipa = self.export('malformed-' + str(index))
                with zipfile.ZipFile(ipa, 'a') as archive:
                    archive.writestr(name, b'')
                manifest_path = ipa.parent / build_ipa.MANIFEST
                manifest = json.loads(manifest_path.read_text())
                manifest['ipa'].update(sha256=build_ipa.digest(ipa), size=ipa.stat().st_size)
                manifest_path.write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                    build_ipa.verify_resign(ipa, manifest_path)

    def test_no_overwrite_and_tampered_archive_rejected(self):
        ipa = self.export('first')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.export('first')
        ipa.write_bytes(ipa.read_bytes() + b'tamper')
        with self.assertRaisesRegex(ValueError, 'hash or size'):
            build_ipa.verify_resign(ipa, ipa.parent / build_ipa.MANIFEST)


if __name__ == '__main__':
    unittest.main(verbosity=2)
