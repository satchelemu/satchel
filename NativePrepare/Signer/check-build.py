#!/usr/bin/env python3
"""Inspect an unsigned Release artifact without accessing a signing identity."""
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys

out = Path(sys.argv[1])
app = out / 'DerivedData/Build/Products/Release-iphoneos/SatchelDev.app'
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == 'com.dell.satchel.dev'
assert info['CFBundleDisplayName'] == 'Satchel Dev'
assert set(info['UISupportedInterfaceOrientations']) == {'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'}
exe = app / info['CFBundleExecutable']
with exe.open('rb') as stream:
    magic, cpu, _, kind, count, size, _, _ = struct.unpack('<8I', stream.read(32))
    assert magic == 0xfeedfacf and cpu == 0x100000c and kind == 2
    commands = stream.read(size)
sections = {}
at = 0
for _ in range(count):
    cmd, length = struct.unpack_from('<II', commands, at)
    assert length >= 8 and at + length <= len(commands)
    if cmd == 0x19:
        row = struct.unpack_from('<II16s4Q4I', commands, at)
        for i in range(row[-2]):
            section = struct.unpack_from('<16s16sQQ8I', commands, at + 72 + 80 * i)
            sections[section[0].rstrip(b'\0').decode()] = section[3]
    at += length
assert sections.get('swhle', 0) > 0 and sections.get('swforce', 0) > 0
assert not any(name.startswith('__dm') for name in sections)
symbols = subprocess.check_output(['nm', str(exe)], text=True)
for name in ['_OBJC_CLASS_$_SatchelSigningIdentity', '_CMS_verify', '_CMS_sign', '_nr_rewrite', '_cs_disasm',
             '_SatchelNativeSessionStarted', '_SatchelMenuSetCustomView', '_SatchelPlayerSettings', '_dm_main']:
    assert any(line.endswith(' ' + name) for line in symbols.splitlines()), name
linked = subprocess.check_output(['otool', '-L', str(exe)], text=True)
assert 'libcrypto' not in linked and 'libcapstone' not in linked
root = Path(__file__).resolve().parents[2]
assert (app / 'PrepareAssets/zsign-LICENSE.txt').read_bytes() == (root / 'NativePrepare/Signer/Vendor/zsign/LICENSE').read_bytes()
assert (app / 'PrepareAssets/OpenSSL-LICENSE.txt').read_bytes() == (root / 'NativePrepare/Signer/Vendor/OpenSSL-LICENSE.txt').read_bytes()
for pattern in ['*.p12', '*.pfx', '*-key.pem', '*.patched.bin', 'romfs.img', 'game.dylib']:
    assert not list(app.rglob(pattern)), pattern
assert not list(app.rglob('b-26dc2fa9477c'))
for listing in (out / 'DerivedData/Build/Intermediates.noindex').rglob('*.SwiftFileList'):
    for line in listing.read_text().splitlines():
        if '/App/' in line or '/NativeHost/' in line or '/NativePrepare/' in line:
            assert str(root) in line, 'Swift source outside task checkout'
result = {'status': 'built_unsigned_not_installed', 'configuration': 'Release', 'architecture': 'arm64',
          'bundleID': info['CFBundleIdentifier'], 'landscape': True, 'hostAndSignerSymbols': 'pass',
          'staticCrypto': True, 'licenses': 'pass', 'noPrivatePayloads': True,
          'appSignatureAndProvisioning': 'skipped, unsigned build', 'ranOnPhone': False, 'played': False}
(out / 'unsigned-app-check.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: unsigned Release artifact, landscape, host and signer linkage, static crypto, licenses and source paths')
