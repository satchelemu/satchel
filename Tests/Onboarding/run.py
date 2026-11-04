#!/usr/bin/env python3
"""Synthetic public-signature metadata and key-format tests. No user files or Keychain writes."""
from pathlib import Path
import datetime
import os
import plistlib
import runpy
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).expanduser().resolve()
assert not out.is_relative_to(root)
out.mkdir(parents=True, exist_ok=True)
ssl = Path('/opt/homebrew/opt/openssl@3')
openssl = ssl / 'bin/openssl'

def run(args):
    result = subprocess.run([str(x) for x in args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        print(result.stderr)
        raise SystemExit(result.returncode)

# Throwaway self-signed test identities remain only in the task's scratch output.
for name, days in [('signer', 2), ('unrelated', 1)]:
    run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-noenc', '-keyout', out / (name + '.key'),
         '-out', out / (name + '.pem'), '-days', days, '-subj', '/CN=Synthetic expiry test ' + name])
    os.chmod(out / (name + '.key'), 0o600)
(out / 'profile.plist').write_bytes(plistlib.dumps({'ExpirationDate': datetime.datetime.fromtimestamp(1800000000, datetime.timezone.utc).replace(tzinfo=None)}))
(out / 'content').write_bytes(b'Synthetic code directory, no executable code')
for name, content, flags in [('profile', 'profile.plist', ['-nodetach']), ('signature', 'content', []),
                             ('no-cert', 'content', ['-nocerts']), ('extra-cert', 'content', ['-certfile', out / 'unrelated.pem'])]:
    run([openssl, 'cms', '-sign', '-binary', '-in', out / content, '-signer', out / 'signer.pem',
         '-inkey', out / 'signer.key', '-outform', 'DER', '-out', out / (name + '.cms'), *flags])
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
run(['nice', '-n', '10', 'xcrun', 'clang++', '-std=c++17', '-fobjc-arc', '-I' + str(ssl / 'include'),
     '-c', root / 'NativePrepare/Signer/SignatureMetadata.mm', '-o', out / 'metadata.o'])
run(['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2', '-warnings-as-errors',
     '-import-objc-header', root / 'NativePrepare/Signer/SignatureMetadata.h',
     root / 'App/SignatureExpiry.swift', root / 'App/FirstRunHelp.swift', root / 'App/SwitchKeys.swift',
     root / 'Tests/Onboarding/Tests.swift', out / 'metadata.o', '-L' + str(ssl / 'lib'), '-lcrypto', '-lc++',
     '-framework', 'Foundation', '-framework', 'Security', '-o', out / 'onboarding-tests'])
subprocess.run([str(out / 'onboarding-tests'), str(out)], check=True)
