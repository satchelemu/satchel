#!/usr/bin/env python3
"""Audit the actual unsigned app product, identities, filenames, exports and strings."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import rename_to_satchel as rename

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-path', action='append', default=[], help='Actual temporary trial path, reported as an exception')
    parser.add_argument('--openssl-archive', type=Path, help='Existing linked third-party archive for exact configured-path provenance')
    args = parser.parse_args()
    app = args.app.resolve()
    assert app.is_dir() and app.name == 'Satchel.app', 'Expected built Satchel.app'
    info = plistlib.loads((app/'Info.plist').read_bytes())
    notices = json.loads(Path(__file__).with_name('preserved-notices.json').read_text())
    assert info['CFBundleIdentifier'] == 'com.dell.satchel', info['CFBundleIdentifier']
    assert info['CFBundleName'] == info['CFBundleDisplayName'] == 'Satchel'
    report = {'app': str(app), 'identity': {key: info[key] for key in
              ['CFBundleIdentifier','CFBundleName','CFBundleDisplayName','CFBundleExecutable']},
              'filenameMatches': [], 'plistMatches': [], 'exportMatches': [], 'stringMatches': [],
              'machObjects': [], 'files': 0}
    openssl_paths = set()
    if args.openssl_archive:
        archive = args.openssl_archive.resolve()
        with archive.open('rb') as stream:
            archive_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        values = subprocess.check_output(['strings','-a',str(archive)],text=True,errors='replace')
        suffixes = ['lib/engines-3','lib/ossl-modules','ssl','ssl/cert.pem','ssl/certs','ssl/ct_log_list.cnf','ssl/private']
        openssl_paths = {value for value in values.splitlines() if value.startswith('/') and
                         any(value.endswith('/openssl-ios/'+suffix) for suffix in suffixes)}
        report['preservedOpenSSL'] = {'archive':str(archive),'sha256':archive_hash,'configuredPaths':sorted(openssl_paths)}
    magic = {bytes.fromhex(value) for value in ['feedface','feedfacf','cefaedfe','cffaedfe','cafebabe','bebafeca','cafebabf','bfbafeca']}
    for path in sorted(app.rglob('*')):
        relative = path.relative_to(app).as_posix()
        if rename.MATCH.search(relative): report['filenameMatches'].append(relative)
        if not path.is_file(): continue
        report['files'] += 1
        data = path.read_bytes()
        if path.suffix == '.plist':
            parsed = plistlib.loads(data)
            if rename.MATCH.search(str(parsed)): report['plistMatches'].append({'file': relative, 'values': parsed})
        if data[:4] in magic:
            report['machObjects'].append(relative)
            symbols = subprocess.check_output(['nm','-gU',str(path)],text=True)
            report['exportMatches'].extend({'file':relative,'symbol':line.split()[-1]} for line in symbols.splitlines() if rename.MATCH.search(line))
        strings = subprocess.check_output(['strings','-a',str(path)],text=True,errors='replace')
        found = {line for line in strings.splitlines() if rename.MATCH.search(line)}
        # Cover wide strings that the platform strings utility may not expose.
        import re
        for encoding in ['utf-16le','utf-16be']:
            wide = data[:len(data)//2*2].decode(encoding, errors='replace')
            found.update(piece for piece in re.findall(r'[ -~]{4,}',wide) if rename.MATCH.search(piece))
        for value in sorted(found):
            rest = value
            for prefix in args.build_path: rest = rest.replace(prefix, '<trial-build-path>')
            reason = 'Temporary trial build path; real roots move before the final rebuild' if not rename.MATCH.search(rest) else None
            if value in openssl_paths:
                reason = 'Configured installation default in the unchanged, already-built OpenSSL libcrypto.a; exact string verified in the linked third-party archive. Root relocation alone does not rewrite this vendor binary.'
            if reason is None and relative in notices:
                notice = notices[relative]
                if hashlib.sha256(data).hexdigest() == notice['sha256']:
                    reason = notice['reason'] + ' Source: ' + notice['source']
            report['stringMatches'].append({'file':relative,'value':value,'reason':reason})
    report['unclassifiedStringMatches'] = sum(row['reason'] is None for row in report['stringMatches'])
    report['passed'] = not any(report[key] for key in ['filenameMatches','plistMatches','exportMatches','unclassifiedStringMatches'])
    args.output.write_text(json.dumps(report,indent=2,default=str)+'\n')
    print(json.dumps({key:value for key,value in report.items() if key != 'stringMatches'},indent=2))
    print('String exceptions:',len(report['stringMatches']))
    raise SystemExit(0 if report['passed'] else 1)

if __name__ == '__main__': main()
