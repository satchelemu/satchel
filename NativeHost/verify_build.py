#!/usr/bin/env python3
"""Check the signed Release artifact without contacting a device."""
from pathlib import Path
import fnmatch, json, os, plistlib, runpy, struct, subprocess
from build import OUT, PIN, ROOT

app_name = os.environ.get('SATCHEL_VERIFY_APP', 'SatchelDev')
bundle_id = os.environ.get('SATCHEL_VERIFY_BUNDLE', 'com.dell.satchel.dev')
display_name = os.environ.get('SATCHEL_VERIFY_DISPLAY', 'Satchel Dev')
migration = os.environ.get('SATCHEL_VERIFY_MIGRATION') == '1'
app = OUT / 'DerivedData/Build/Products/Release-iphoneos' / (app_name + '.app')
subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app], check=True)
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == bundle_id
assert info['CFBundleDisplayName'] == display_name
assert set(info['UISupportedInterfaceOrientations']) == {'UIInterfaceOrientationLandscapeRight', 'UIInterfaceOrientationLandscapeLeft'}
assert info['DMCommit'] == PIN[:12] and info['DMChain'] == 'e65a4ebd3322'
signed = plistlib.loads(subprocess.run(['codesign', '-d', '--entitlements', ':-', app], capture_output=True, check=True).stdout)
profile = plistlib.loads(subprocess.run(['security', 'cms', '-D', '-i', app / 'embedded.mobileprovision'], capture_output=True, check=True).stdout)
# The team comes from the artifact itself; the repository holds no team ID.
team = profile['TeamIdentifier'][0]
# This file is also exec'd by build-dev.py, where __file__ is undefined: use the host module's ROOT.
expected = runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['signing_team'](ROOT, required=False)
assert expected is None or expected == team, 'Signed by team ' + team + ', expected ' + expected
for ent in [signed, profile['Entitlements']]:
    assert ent['application-identifier'] == team + '.' + bundle_id
    assert ent['com.apple.developer.team-identifier'] == team
    assert ent['com.apple.developer.kernel.increased-memory-limit'] is True
    assert ent['com.apple.developer.kernel.extended-virtual-addressing'] is True
if migration:
    assert info['SatchelMigrationBridge'] is (bundle_id == 'com.dell.satchel.dev')
    assert info['BGTaskSchedulerPermittedIdentifiers'] == [bundle_id + '.prepare.*']
    assert info['SatchelPrivateKeychainGroup'] == team + '.' + bundle_id
    assert set(signed['keychain-access-groups']) == {team + '.' + bundle_id, team + '.com.dell.satchel.migration'}
    for ent in [signed, profile['Entitlements']]:
        assert 'group.com.dell.satchel.migration' in ent['com.apple.security.application-groups']
        for group in [team + '.' + bundle_id, team + '.com.dell.satchel.migration']:
            assert any(fnmatch.fnmatchcase(group, pattern) for pattern in ent['keychain-access-groups'])
    if bundle_id == 'com.dell.satchel':
        assert info['CFBundleIcons']['CFBundlePrimaryIcon']['CFBundleIconName'] == 'AppIcon'
exe = app / info['CFBundleExecutable']
with exe.open('rb') as f:
    magic, cpu, sub, kind, count, size, flags, reserved = struct.unpack('<8I', f.read(32))
    assert magic == 0xfeedfacf and cpu == 0x100000c and kind == 2
    commands = f.read(size)
sections, at = {}, 0
for _ in range(count):
    cmd, length = struct.unpack_from('<II', commands, at)
    if cmd == 0x19:
        row = struct.unpack_from('<II16s4Q4I', commands, at)
        for i in range(row[-2]):
            sec = struct.unpack_from('<16s16sQQ8I', commands, at + 72 + 80 * i)
            sections[sec[0].rstrip(b'\0').decode()] = sec[3]
    at += length
assert sections.get('swhle', 0) > 0 and sections.get('swforce', 0) > 0, 'Host registration entries were stripped'
assert not any(k.startswith('__dm') for k in sections), 'Guest module sections found in the app executable'
symbols = subprocess.check_output(['nm', '-gU', exe], text=True)
for name in ['_dm_main', '_dm_set_game_image_header', '_SatchelPrepareNativeSession', '_unimpl_stubs', '_front_metal_layer']:
    assert any(line.endswith(' ' + name) for line in symbols.splitlines()), 'Missing linked host symbol ' + name
assert not list(app.rglob('*.patched.bin')) and not list(app.rglob('romfs.img'))
assert not list(app.rglob('game.dylib')) and not list(app.rglob('b-e65a4ebd3322'))
result = {'status': 'built_signed_verified_not_installed', 'app': str(app), 'configuration': 'Release',
          'bundleID': info['CFBundleIdentifier'], 'displayName': info['CFBundleDisplayName'], 'enginePin': PIN,
          'shaderChain': info['DMChain'], 'team': team, 'signatureEntitlements': signed,
          'hostRegistrationBytes': {k:sections[k] for k in ['swhle','swforce']},
          'guestModuleSectionsInExecutable': False, 'deviceActions': 'none', 'played': False}
(OUT / 'signed-app-metadata.json').write_text(json.dumps(result, indent=2)+'\n')
print('PASS: signed Release app, both memory entitlements, landscape, host entries retained, game module sections absent')
