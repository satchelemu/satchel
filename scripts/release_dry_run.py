#!/usr/bin/env python3
"""Release dry run: every artifact a release publishes, written locally, nothing published.

From a clean, committed tree that passes the hygiene check, and a clean unsigned build of
that commit, this writes into a fresh --out folder:

    release/Satchel.ipa               unsigned IPA (scripts/build_ipa.py)
    release/Satchel.manifest.json     IPA hash and every packaged file's hash and mode
    release/Satchel.entitlements      account-neutral entitlement template
    release/SHA256SUMS                hashes of the three release assets above
    release/source.json               the SideStore/AltStore source, published layout
    release/source-entry.json         this version's entry alone
    release/RELEASE_NOTES.md          from ROADMAP.md and COMPATIBILITY.md at this commit
    release/PUBLISHING_CHECKLIST.md   the manual steps, filled in with this release's values
    dry-run.json                      what was checked, with every result
    logs/                             build and check logs

The build comes from one of two places:

    (default)          NativeApp/build.py --unsigned, run here under NativePrepare/guard.py
                       against the dependency root already in the environment
                       (SATCHEL_DEPS_ROOT and the other variables in BUILDING.md)
    --clean-work DIR   an existing scripts/clean_build.py work folder whose --check passes
                       and whose app step was built from this exact commit

The whole run happens inside sandbox-exec with outbound IP traffic denied, the same policy
the dependency recipes use. It creates no tag or release, uploads nothing, never runs gh or
git fetch, and signs nothing except the disposable ad hoc copy that build_ipa.py checks.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import urlsplit

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'NativeApp/Distribution'))
import build_ipa  # noqa: E402
import source as distribution  # noqa: E402
from schema import validate as schema_validate  # noqa: E402

# scripts/bootstrap_deps.py's policy (no outbound IP to any address, local IPC allowed),
# plus two exact binaries that may start outside it, because a sandboxed process cannot
# run them: the build guard's process listing (/bin/ps is set-user-ID) and the Swift
# compiler's macro plugin launcher (swift-frontend starts its plugin server through
# /usr/bin/sandbox-exec with Swift's own profile, and a sandbox cannot be applied inside
# another). Every other process, including git, xcodebuild and codesign, stays inside.
OFFLINE_POLICY = '(version 1) (allow default) (deny network-outbound (remote ip "*:*"))'
EXCEPTIONS = ['/bin/ps', '/usr/bin/sandbox-exec']
RELEASE_POLICY = OFFLINE_POLICY + ''.join(' (allow process-exec (literal "%s") (with no-sandbox))' % path
                                          for path in EXCEPTIONS)
GIB = 1024 ** 3
DEPENDENCY_VARIABLES = ['SATCHEL_DEPS_ROOT', 'SATCHEL_NATIVE_BUILD_INPUTS', 'SATCHEL_CAPSTONE_SOURCE',
                        'SATCHEL_OPENSSL_IOS', 'SATCHEL_SHADER_DEPS']
# Same file types the hygiene check refuses to track. Ignored copies of them inside the
# folders the build reads would reach the app, so they are refused too.
FORBIDDEN_FILES = re.compile(r'(^|/)(prod|title)\.keys$|\.(p12|pfx|mobileprovision|xci|xcz|nsp|nsz|nca|ncz|ipa|dylib|metallib)$',
                             re.IGNORECASE)
# Private folders the build never reads (the maintainer's notes and agent tooling).
PRIVATE_FOLDERS = ['local', 'notes', 'private', 'scratch', '.claude', '.codex', '.cursor']
ASSETS = ['Satchel.ipa', build_ipa.MANIFEST, build_ipa.ENTITLEMENTS]
BUILD_FOLDERS = ['build', 'build-root', 'guard', 'tmp']


class Refused(Exception):
    pass


def refuse(condition, message):
    if not condition:
        raise Refused(message)


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, check=True).stdout


def tree_state():
    head = git('rev-parse', 'HEAD').strip()
    status = git('status', '--porcelain=v1', '--untracked-files=all', '--ignore-submodules=none')
    return head, status


def sandboxed():
    """True when this process runs under a sandbox profile (sandbox_check with no filter)."""
    try:
        check = ctypes.CDLL('/usr/lib/libSystem.B.dylib').sandbox_check
        check.restype = ctypes.c_int
        check.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        return check(os.getpid(), None, 0) == 1
    except (OSError, AttributeError):
        return False


def sha256(path):
    return build_ipa.digest(path)


def check_tree():
    """Refuse a dirty tree, a failing hygiene check or forbidden ignored files."""
    head, status = tree_state()
    refuse(not status.strip(), 'The tree has uncommitted or untracked changes; commit or remove them first:\n'
           + status[:2000])
    hygiene = subprocess.run(['sh', str(ROOT / 'scripts/check_repo_hygiene.sh')], cwd=str(ROOT),
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    refuse(hygiene.returncode == 0, 'scripts/check_repo_hygiene.sh failed:\n' + hygiene.stdout[-4000:])
    ignored = git('ls-files', '--others', '--ignored', '--exclude-standard', '--', '.',
                  *[':!' + name for name in PRIVATE_FOLDERS]).splitlines()
    found = [path for path in ignored if FORBIDDEN_FILES.search(path)]
    refuse(not found, 'Ignored key, dump, identity or prepared files inside the checkout; move them out first:\n'
           + '\n'.join(found[:20]))
    return head, hygiene.stdout.strip().splitlines()[-1]


def check_tag(tag, head):
    """An existing local tag must point at this commit and match release.json's tag template."""
    metadata = distribution.release_metadata()
    expected = distribution.release_layout(metadata['version'], metadata['build'], metadata)['tag']
    refuse(tag == expected, 'Tag %s does not match release.json (expected %s)' % (tag, expected))
    try:
        target = git('rev-parse', '--verify', '--quiet', 'refs/tags/' + tag + '^{commit}').strip()
    except subprocess.CalledProcessError:
        target = ''
    refuse(target == head, 'Tag %s does not point at this commit %s' % (tag, head))


def check_out(out):
    refuse(out != ROOT and ROOT not in out.parents and out not in ROOT.parents,
           'The output folder must be outside the checkout')
    refuse(not out.exists() or (out.is_dir() and not any(out.iterdir())),
           'The output folder must be new or empty: ' + str(out))


def run_logged(command, log, env=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as stream:
        return subprocess.run([str(c) for c in command], cwd=str(ROOT), env=env, stdout=stream,
                              stderr=subprocess.STDOUT).returncode


def build_here(out, head):
    missing = [name for name in DEPENDENCY_VARIABLES if not os.environ.get(name) or not Path(os.environ[name]).exists()]
    refuse(not missing, 'Set the dependency root first (BUILDING.md): missing ' + ', '.join(missing))
    refuse(shutil.disk_usage(str(out)).free >= 36 * GIB, 'Disk reserve: the build guard needs 36 GiB free')
    for name in BUILD_FOLDERS:
        (out / name).mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SATCHEL_OUT=str(out / 'build'), SATCHEL_BUILD_ROOT=str(out / 'build-root'),
               ANYGAME_OUT=str(out / 'guard'), TMPDIR=str(out / 'tmp') + '/', PYTHONDONTWRITEBYTECODE='1')
    for name in ['SATCHEL_HOST_OUT', 'SATCHEL_PREPARE_OUT', 'SATCHEL_BUILD_SLOT']:
        env.pop(name, None)
    code = run_logged([sys.executable, '-B', ROOT / 'NativePrepare/guard.py', sys.executable, '-B',
                       ROOT / 'NativeApp/build.py', '--unsigned'], out / 'logs/app.log', env)
    refuse(code == 0, 'The unsigned app build failed (exit %d); see %s' % (code, out / 'logs/app.log'))
    return out / 'build', {'mode': 'built here', 'dependencyRoot': 'SATCHEL_DEPS_ROOT from the environment',
                           'log': 'logs/app.log'}


def build_from_clean_work(work, out, head):
    code = run_logged([sys.executable, '-B', ROOT / 'scripts/clean_build.py', '--work', work, '--check'],
                      out / 'logs/clean-check.log')
    refuse(code == 0, 'scripts/clean_build.py --check does not pass for ' + str(work)
           + '; see ' + str(out / 'logs/clean-check.log'))
    record = json.loads((work / 'logs/app.json').read_text())
    refuse(record.get('exit') == 0 and record.get('source') == head,
           'The clean work folder app was built from %s, not this commit %s' % (record.get('source'), head))
    return work / 'out', {'mode': 'clean_build.py work folder', 'work': str(work), 'check': 'logs/clean-check.log'}


def table(markdown, heading):
    lines = markdown.splitlines()
    refuse(heading in lines, 'Missing section ' + heading)
    rows = []
    for line in lines[lines.index(heading) + 1:]:
        if line.startswith('|'):
            rows.append([cell.strip() for cell in line.strip().strip('|').split('|')])
        elif rows:
            break
    refuse(len(rows) >= 3 and set(''.join(rows[1])) <= set('-: '), 'Unexpected table under ' + heading)
    header = rows[0]
    return [dict(zip(header, row)) for row in rows[2:] if len(row) == len(header)]


def release_notes(info, layout, head, ipa, manifest_sha):
    roadmap = table((ROOT / 'ROADMAP.md').read_text(), '## Progress by area')
    games = table((ROOT / 'COMPATIBILITY.md').read_text(), '## Tested games')
    distribution_row = next(row for row in roadmap if row['Area'].startswith('Distribution'))
    repo, tag = layout['repository'], layout['tag']
    blob = 'https://github.com/' + repo + '/blob/' + tag + '/'
    lines = ['# Satchel ' + info['CFBundleShortVersionString'] + ' (' + info['CFBundleVersion'] + ')', '',
             'An unsigned IPA for sideloading with your own Apple account (SideStore, AltStore Classic or '
             'Xcode). Built from commit `' + head + '`, tagged `' + tag + '`.', '',
             '## Install', '',
             '- Add this source in SideStore or AltStore Classic: ' + layout['source'],
             '- Or download `Satchel.ipa` from this release and install it from Files.',
             '- Guide: ' + blob + 'docs/install.md',
             '- Requires iOS ' + info['MinimumOSVersion'] + ' or later. Satchel requests the increased memory '
             'limit and extended virtual addressing; your installer has to keep both '
             '(' + blob + 'docs/distribution-validation.md).',
             '- No keys, firmware or games are included. You supply your own dumps and `prod.keys`.', '',
             '## Integrity', '',
             '- `Satchel.ipa`: SHA-256 `' + sha256(ipa) + '`, ' + str(ipa.stat().st_size) + ' bytes.',
             '- `Satchel.manifest.json` (SHA-256 `' + manifest_sha + '`) lists every packaged file with its '
             'SHA-256, size and mode. `SHA256SUMS` covers the three release assets.', '',
             '## Where things stand', '',
             'From [ROADMAP.md](' + blob + 'ROADMAP.md) at this commit, where each area has its full status.', '',
             '| Area | Done |', '|---|---|']
    lines += ['| ' + row['Area'] + ' | ' + row['Done'] + ' |' for row in roadmap]
    lines += ['', '## Tested games', '',
              'From [COMPATIBILITY.md](' + blob + 'COMPATIBILITY.md) at this commit.', '',
              '| Game | Version | Rating | Frame rate | Last tested |', '|---|---|---|---|---|']
    lines += ['| ' + ' | '.join(row[key] for key in ['Game', 'Version', 'Rating', 'Frame rate', 'Last tested']) + ' |'
              for row in games]
    lines += ['', '## Distribution', '', distribution_row['Status'], '']
    return '\n'.join(lines)


CHECKLIST = '''# Publishing checklist: Satchel {version} ({build})

Generated by `scripts/release_dry_run.py` from commit `{commit}`. Nothing below has been
done: every step is the maintainer's. The general process is in `docs/publishing.md`.

## 1. Review (before anything leaves the Mac)

- [ ] `NativeApp/Distribution/release.json` at `{commit}` has the version, build and date
      intended for this release (`{version}`, `{build}`, `{date}`), bumped in a reviewed commit.
- [ ] `git log` since the previous release tag reads right; the commit is on `main`.
- [ ] `RELEASE_NOTES.md` reads right.
- [ ] `source.json`: name, description, `{source_url}`, the download URL, size `{size}`
      and SHA-256 `{ipa_sha}` match this folder.
- [ ] `Satchel.entitlements` and the source's `appPermissions` list exactly:
{entitlements}
- [ ] `SHA256SUMS` matches: `shasum -a 256 -c SHA256SUMS` in this folder.
- [ ] Nothing personal: no name, email, team ID or certificate name in the notes, the
      source or the tag message.

## 2. Phone gates (before publishing; docs/distribution-validation.md)

- [ ] This exact `Satchel.ipa` (SHA-256 above) installs with SideStore and with AltStore
      Classic, with your own account, in place over the existing app.
- [ ] Settings > Device check shows increased memory and extended virtual addressing in the
      running signature for each installer. If an installer drops either, stop.
- [ ] Refresh with each installer advances the expiry and keeps the library and saves.

## 3. Publish (each step is one reviewed action; none is run by any tool here)

- [ ] Tag the reviewed commit and push the tag:
      `git tag -a {tag} {commit} -m "Satchel {version} ({build})"` then
      `git push origin {tag}`.
- [ ] Create the GitHub release for `{tag}` on `{repository}` with `RELEASE_NOTES.md` as its
      text and these assets, unchanged: `Satchel.ipa`, `Satchel.manifest.json`,
      `Satchel.entitlements`, `SHA256SUMS`. For example, from this folder:
      `gh release create {tag} --repo {repository} --verify-tag --title "Satchel {version} ({build})" --notes-file RELEASE_NOTES.md Satchel.ipa Satchel.manifest.json Satchel.entitlements SHA256SUMS`
- [ ] Download the published asset and compare: {download_url}
      must have SHA-256 `{ipa_sha}`.
- [ ] Copy this folder's `source.json` to `{source_path}` at the repository root of `main`,
      commit it on its own and push. It must be published after the release asset exists,
      because it points at it.
- [ ] Check that {source_url} serves exactly this `source.json`, and that the icon and
      screenshot URLs in it load (they are served from the `{tag}` tag).

## 4. After publishing

- [ ] On the phone, add {source_url} in SideStore and in AltStore Classic (Browse, Sources)
      and update Satchel from the source; repeat the Device check.
- [ ] Record the release in ROADMAP.md (Distribution row) and README.md.

## Values

| Field | Value |
|---|---|
| Commit | `{commit}` |
| Tag | `{tag}` |
| Download | {download_url} |
| Source | {source_url} |
| Icon | {icon_url} |
| IPA size | {size} bytes |
| IPA SHA-256 | `{ipa_sha}` |
| Manifest SHA-256 | `{manifest_sha}` |
| Minimum iOS | {minimum} |
'''


def no_dash_punctuation(text):
    return chr(0x2014) not in text and ' ' + '-' * 2 + ' ' not in text


def package(build_root, out, head, provenance):
    release = out / 'release'
    checks = []

    def passed(name, detail):
        checks.append({'check': name, 'pass': True, 'detail': detail})
        print('PASS ' + name + ': ' + detail, flush=True)

    ipa = build_ipa.export(build_root, release)
    manifest_path = release / build_ipa.MANIFEST
    passed('ipa', 'Satchel.ipa %d bytes, SHA-256 %s' % (ipa.stat().st_size, sha256(ipa)))
    build_ipa.verify_resign(ipa, manifest_path)
    passed('resign', 'ad hoc re-sign of a disposable copy keeps every requested entitlement value; IPA unchanged')
    # Packaging reproducibility: a second export of the same build must give the same bytes.
    repeat = out / 'repeat'
    build_ipa.export(build_root, repeat)
    same = all((repeat / name).read_bytes() == (release / name).read_bytes() for name in ASSETS)
    shutil.rmtree(str(repeat))
    refuse(same, 'A second export of the same build gave different bytes')
    passed('repeat', 'a second export of the same build is byte-identical (IPA, manifest, entitlements)')

    source = distribution.make_source(ipa)
    distribution.validate_source(source)
    schema_validate(source)
    refuse(distribution.make_source(ipa) == source, 'Source generation is not stable')
    app = source['apps'][0]
    info, _, _, _ = distribution.inspect_ipa(ipa)
    layout = distribution.release_layout(info['CFBundleShortVersionString'], info['CFBundleVersion'])
    urls = [source['sourceURL'], app['iconURL'], app['downloadURL']] + app['screenshotURLs']
    refuse(not any('example.invalid' in link for link in urls), 'Placeholder URL in the source')
    # The icon and screenshots are served from the tag: each must be a file tracked at this commit.
    prefix = '/' + layout['repository'] + '/' + layout['tag'] + '/'
    tracked = [urlsplit(link).path.split(prefix, 1)[1] for link in [app['iconURL']] + app['screenshotURLs']]
    for path in tracked:
        refuse(subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', head + ':' + path]).returncode == 0,
               'Not tracked at this commit: ' + path)
    (release / 'source.json').write_text(json.dumps(source, indent=2, ensure_ascii=False) + '\n')
    (release / 'source-entry.json').write_text(json.dumps(app['versions'][0], indent=2) + '\n')
    passed('source', 'schema and every field match the IPA; ' + source['sourceURL'])

    manifest_sha = sha256(manifest_path)
    sums = ''.join(sha256(release / name) + '  ' + name + '\n' for name in ASSETS)
    (release / 'SHA256SUMS').write_text(sums)
    notes = release_notes(info, layout, head, ipa, manifest_sha)
    entitlements = '\n'.join('      `' + name + '`' for name in app['appPermissions']['entitlements'])
    checklist = CHECKLIST.format(
        version=info['CFBundleShortVersionString'], build=info['CFBundleVersion'], commit=head,
        date=app['versionDate'], tag=layout['tag'], repository=layout['repository'],
        source_url=source['sourceURL'], download_url=app['downloadURL'], icon_url=app['iconURL'],
        source_path=layout['sourcePath'], size=ipa.stat().st_size, ipa_sha=sha256(ipa),
        manifest_sha=manifest_sha, minimum=info['MinimumOSVersion'], entitlements=entitlements)
    for name, text in [('RELEASE_NOTES.md', notes), ('PUBLISHING_CHECKLIST.md', checklist)]:
        refuse(no_dash_punctuation(text), name + ' would contain an em dash or a double hyphen')
        (release / name).write_text(text)
    passed('notes', 'RELEASE_NOTES.md from ROADMAP.md and COMPATIBILITY.md; PUBLISHING_CHECKLIST.md')
    return {'commit': head, 'version': info['CFBundleShortVersionString'], 'build': info['CFBundleVersion'],
            'tag': layout['tag'], 'build_source': provenance,
            'assets': {name: {'sha256': sha256(release / name), 'size': (release / name).stat().st_size}
                       for name in ASSETS + ['SHA256SUMS', 'source.json', 'source-entry.json',
                                             'RELEASE_NOTES.md', 'PUBLISHING_CHECKLIST.md']},
            'urls': {'source': source['sourceURL'], 'download': app['downloadURL'], 'icon': app['iconURL'],
                     'screenshots': app['screenshotURLs']},
            'checks': checks}


def dry_run(args):
    out = Path(os.path.abspath(str(args.out.expanduser())))
    check_out(out)
    head, hygiene = check_tree()
    print('PASS tree: commit %s, no uncommitted or untracked files; %s' % (head, hygiene), flush=True)
    if args.expect_tag:
        check_tag(args.expect_tag, head)
        print('PASS tag: ' + args.expect_tag + ' names this commit and release.json', flush=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'logs').mkdir()
    if args.clean_work:
        build_root, provenance = build_from_clean_work(Path(os.path.abspath(str(args.clean_work.expanduser()))), out, head)
    else:
        build_root, provenance = build_here(out, head)
    refuse(tree_state() == (head, ''), 'The tree changed during the build')
    print('PASS build: ' + provenance['mode'] + ' from ' + head, flush=True)
    record = package(build_root, out, head, provenance)
    refuse(tree_state() == (head, ''), 'The tree changed during packaging')
    record['checks'][:0] = [{'check': 'tree', 'pass': True, 'detail': hygiene},
                            {'check': 'build', 'pass': True, 'detail': provenance['mode']}]
    record['network'] = 'sandbox-exec: ' + RELEASE_POLICY
    if not args.clean_work and not args.keep_build:
        for name in BUILD_FOLDERS:
            shutil.rmtree(str(out / name), ignore_errors=True)
        record['build_source']['kept'] = False
    (out / 'dry-run.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Dry run complete: ' + str(out / 'release') + '. Nothing was published; see PUBLISHING_CHECKLIST.md.')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, required=True, help='New or empty folder outside the checkout')
    parser.add_argument('--clean-work', type=Path, help='Use a scripts/clean_build.py work folder instead of building here')
    parser.add_argument('--keep-build', action='store_true', help='Keep the app build under <out>/build afterwards')
    parser.add_argument('--expect-tag', help='Also require this existing local tag to name HEAD and match release.json')
    parser.add_argument('--network-denied', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.network_denied:
        # Re-run inside the offline sandbox; every child (git, xcodebuild, codesign) inherits it.
        return subprocess.call(['/usr/bin/sandbox-exec', '-p', RELEASE_POLICY, sys.executable, '-B',
                                str(Path(__file__).resolve()), *sys.argv[1:], '--network-denied'])
    if not sandboxed():
        parser.exit(1, 'ERROR: not running inside the offline sandbox\n')
    os.nice(10)
    try:
        return dry_run(args)
    except (Refused, ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'REFUSED: ' + str(error).strip() + '\n')


if __name__ == '__main__':
    sys.exit(main())
