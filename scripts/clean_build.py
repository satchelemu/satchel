#!/usr/bin/env python3
"""The clean-Mac route: from a fresh clone to an unsigned Satchel.app and IPA.

Run it with the Python that ships with Xcode (3.9 or newer):

    /usr/bin/python3 scripts/clean_build.py --work ~/SatchelClean

Steps, in order (each can be selected with --steps):

    tools  scripts/setup_tools.py: check Xcode, install pinned CMake, XcodeGen, .NET SDK,
           Python, spirv-val and the fontTools wheel into <work>/tools
    deps   scripts/bootstrap_deps.py --tools --fetch: fetch every pinned input from its
           official URL, verify its hash, then build all dependencies offline into <work>/deps
    app    NativeApp/build.py --unsigned against that root, output in <work>/out
    ipa    scripts/build_ipa.py: <work>/ipa/Satchel.ipa and its per-file hash manifest
    check  report which steps pass (also available alone with --check)

Every step runs with a minimal environment: PATH is <work>/tools/bin and the macOS system
folders only, and HOME is <work>/home, so nothing from Homebrew, /usr/local, ~/SatchelDeps,
~/SatchelBuild or any other part of your account can be picked up. Nothing is signed,
installed or launched.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import pwd
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STEPS = ['tools', 'deps', 'app', 'ipa']
SYSTEM_PATH = '/usr/bin:/bin:/usr/sbin:/sbin'
# Where a clean build may legitimately point besides the checkout and the work folder.
SYSTEM_PREFIXES = ['/usr/', '/bin/', '/sbin/', '/System/', '/Library/Developer/', '/private/var/', '/var/', '/dev/']
FOREIGN = [re.compile(r'/opt/homebrew\b'), re.compile(r'/usr/local/(?:bin|lib|include|opt|Cellar)\b')]


def layout(work):
    return {name: work / name for name in ['tools', 'deps', 'out', 'ipa', 'home', 'tmp', 'logs', 'build-root', 'guard']}


def environment(paths):
    env = {'PATH': str(paths['tools'] / 'bin') + ':' + SYSTEM_PATH, 'HOME': str(paths['home']),
           'TMPDIR': str(paths['tmp']) + '/', 'LANG': 'en_US.UTF-8', 'PYTHONDONTWRITEBYTECODE': '1'}
    for key in ['USER', 'LOGNAME', 'DEVELOPER_DIR', 'TERM']:
        if key in os.environ:
            env[key] = os.environ[key]
    return env


def app_environment(paths):
    deps = paths['deps']
    return dict(environment(paths), SATCHEL_OUT=str(paths['out']), SATCHEL_DEPS_ROOT=str(deps),
                SATCHEL_BUILD_ROOT=str(paths['build-root']), SATCHEL_NATIVE_BUILD_INPUTS=str(deps / 'build-inputs'),
                SATCHEL_CAPSTONE_SOURCE=str(deps / 'sources/capstone-d2e5739cbb43df93d589a3a726c8f878b95beccb'),
                SATCHEL_OPENSSL_IOS=str(deps / 'openssl-ios'), SATCHEL_SHADER_DEPS=str(deps / 'm5'),
                ANYGAME_OUT=str(paths['guard']))


def step(paths, name, command, env):
    log = paths['logs'] / (name + '.log')
    print('Step ' + name + ': ' + ' '.join(str(c) for c in command), flush=True)
    print('  log: ' + str(log), flush=True)
    started = time.time()
    with log.open('w') as stream:
        result = subprocess.run([str(c) for c in command], cwd=str(ROOT), env=env, stdout=stream, stderr=subprocess.STDOUT)
    record = {'exit': result.returncode, 'seconds': round(time.time() - started), 'source': git_head()}
    (paths['logs'] / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    print('  exit %d after %d s' % (result.returncode, record['seconds']), flush=True)
    return result.returncode


def git_head():
    try:
        head = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], stdout=subprocess.PIPE,
                              universal_newlines=True, check=True).stdout.strip()
        dirty = subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain'], stdout=subprocess.PIPE,
                               universal_newlines=True, check=True).stdout.strip()
        return head + (' (with local changes)' if dirty else '')
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def sha256(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def strings(value):
    if isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)
    elif isinstance(value, str):
        yield value


def ipa_rows(manifest):
    """scripts/build_ipa.py lists each packaged file as {path, sha256, size, mode}."""
    return {row['path']: row for row in manifest.get('files', [])}


def compare_ipa(mine, reference):
    rows = {}
    for name in sorted(set(mine) | set(reference)):
        ours, theirs = mine.get(name), reference.get(name)
        rows[name] = ('only in clean build' if theirs is None else 'only in reference' if ours is None else
                      'identical' if ours['sha256'] == theirs['sha256'] else 'different')
    return rows


def foreign_paths(text, allowed, home):
    """Absolute paths in text that are neither system paths nor inside the allowed roots."""
    found = set()
    for pattern in FOREIGN:
        found.update(match.group(0) for match in pattern.finditer(text))
    # Anything in the real account's home folder must be inside the checkout or work folder.
    for match in re.finditer(re.escape(home) + r'/[^\s"\',;:)\]]*', text):
        path = match.group(0)
        if not any(path == root or path.startswith(root + '/') for root in allowed):
            found.add(path)
    return found


def check(paths, reference_manifest=None):
    """Report each step; exit 0 only when every step passes."""
    results = []

    def report(name, ok, detail):
        results.append({'step': name, 'pass': bool(ok), 'detail': detail})
        print(('PASS ' if ok else 'FAIL ') + name + ': ' + detail, flush=True)

    env = environment(paths)
    tools = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/setup_tools.py'), '--tools', str(paths['tools']),
                            '--check-only'], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True)
    manifest = paths['tools'] / 'tools-manifest.json'
    if tools.returncode == 0 and manifest.is_file():
        data = json.loads(manifest.read_text())
        report('xcode', True, data['host']['xcode'] + ', iOS SDK ' + data['host']['ios_sdk'] + ', ' + data['host']['metal'])
        report('tools', True, ', '.join(name + ' ' + row['lock']['version'] for name, row in sorted(data['tools'].items()))
               + ' (pinned hashes verified)')
    else:
        report('tools', False, ' '.join(tools.stdout.split())[-400:] or 'run the tools step')

    state_path = paths['deps'] / 'bootstrap-manifest.json'
    if state_path.is_file():
        state = json.loads(state_path.read_text())
        verified = sum(1 for row in state.get('inputs', {}).values() if row.get('status') == 'verified')
        failed = sorted(name for name, code in state.get('steps', {}).items() if code != 0)
        outputs = state.get('outputs', {'files': {}, 'missing': ['(no output manifest)']})
        declared = len(json.loads((ROOT / 'Tools/Rename/dependency-artifacts.json').read_text())) + 8
        detail = '%d of %d inputs verified, %d recipe steps recorded, %s, ' % (
            verified, len(json.loads((ROOT / 'scripts/deps/inputs.lock.json').read_text())['inputs']),
            len(state.get('steps', {})), 'all exit 0' if not failed else 'failed: ' + ', '.join(failed))
        if 'outputs' in state:
            detail += '%d declared artifacts, %d missing (%d files)' % (declared, len(outputs['missing']), len(outputs['files']))
        else:
            detail += 'no output manifest: the run stopped before finishing (see the deps log)'
        comparison = [row.get('comparison') for row in outputs['files'].values() if row.get('comparison')]
        if comparison:
            detail += '; versus reference: ' + ', '.join('%d %s' % (comparison.count(k), k) for k in sorted(set(comparison)))
        report('deps', state.get('clean_mac_ready') is True and not failed and not outputs['missing'], detail)
    else:
        report('deps', False, 'no ' + str(state_path))

    app = paths['out'] / 'app/DerivedData/Build/Products/Release-iphoneos/Satchel.app'
    if (app / 'Info.plist').is_file():
        info = plistlib.loads((app / 'Info.plist').read_bytes())
        signed = (app / '_CodeSignature').exists() or (app / 'embedded.mobileprovision').exists()
        # XcodeGen warns like this when it cannot find its setting presets; the project
        # then lacks the standard Release settings and the app differs from a normal build.
        log = paths['logs'] / 'app.log'
        presets = not (log.is_file() and 'settings found' in log.read_text(errors='replace'))
        report('app', info.get('CFBundleIdentifier') == 'com.dell.satchel' and not signed and presets,
               '%s %s (%s), %s, %d files%s' % (info.get('CFBundleIdentifier'), info.get('CFBundleShortVersionString'),
                                              info.get('CFBundleVersion'), 'unsigned' if not signed else 'SIGNED',
                                              sum(1 for p in app.rglob('*') if p.is_file()),
                                              '' if presets else ', XcodeGen setting presets missing (see app.log)'))
    else:
        report('app', False, 'no ' + str(app))

    ipa = paths['ipa'] / 'Satchel.ipa'
    ipa_manifest = paths['ipa'] / 'Satchel.manifest.json'
    if ipa.is_file() and ipa_manifest.is_file():
        files = json.loads(ipa_manifest.read_text())
        listed = ipa_rows(files)
        report('ipa', files.get('ipa', {}).get('sha256') == sha256(ipa),
               'Satchel.ipa sha256 %s, %d bytes, %d files in manifest' % (sha256(ipa), ipa.stat().st_size, len(listed)))
        if reference_manifest:
            rows = compare_ipa(listed, ipa_rows(json.loads(Path(reference_manifest).read_text())))
            (paths['logs'] / 'app-comparison.json').write_text(json.dumps(rows, indent=2) + '\n')
            counts = {}
            for value in rows.values():
                counts[value] = counts.get(value, 0) + 1
            print('INFO app versus reference: ' + ', '.join('%d %s' % (n, k) for k, n in sorted(counts.items()))
                  + ' (' + str(paths['logs'] / 'app-comparison.json') + ')', flush=True)
    else:
        report('ipa', False, 'no ' + str(ipa))

    # Isolation: generated projects, logs and link commands may name only the checkout, the
    # work folder and system locations.
    home = pwd.getpwuid(os.getuid()).pw_dir
    allowed = [str(ROOT), str(paths['tools'].parent), os.path.realpath(str(paths['tools'].parent))]
    leaks = {}
    scanned = 0
    candidates = [paths['out'] / 'host/project.json', paths['out'] / 'app/project.json']
    candidates += sorted(paths['logs'].glob('*.log')) + sorted((paths['deps'] / 'bootstrap-logs').glob('*.log'))
    candidates += sorted((paths['deps'] / 'rebuild-logs').glob('*.log'))
    for path in candidates:
        if not path.is_file():
            continue
        scanned += 1
        text = path.read_text(errors='replace')
        if path.suffix == '.json':
            text = '\n'.join(strings(json.loads(text)))
        found = foreign_paths(text, allowed, home)
        if found:
            leaks[str(path)] = sorted(found)[:5]
    binary = app / 'Satchel'
    if binary.is_file():
        linked = subprocess.run(['otool', '-L', str(binary)], stdout=subprocess.PIPE, universal_newlines=True).stdout
        found = foreign_paths(linked, allowed, home)
        found.update(line.split()[0] for line in linked.splitlines()[1:]
                     if line.split() and not line.split()[0].startswith(('@rpath/', '/usr/lib/', '/System/')))
        if found:
            leaks[str(binary)] = sorted(found)
        scanned += 1
    (paths['logs'] / 'isolation.json').write_text(json.dumps(leaks, indent=2) + '\n')
    report('isolation', scanned > 0 and not leaks,
           '%d files scanned, %s' % (scanned, 'no path outside the checkout, the work folder and the system'
                                     if not leaks else 'foreign paths in ' + ', '.join(sorted(leaks))[:400]))
    (paths['logs'] / 'check.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(row['pass'] for row in results) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', type=Path, required=True, help='Work folder outside the checkout (tools, deps, out, ipa, logs)')
    parser.add_argument('--steps', default=','.join(STEPS), help='Comma-separated subset of: ' + ', '.join(STEPS))
    parser.add_argument('--check', action='store_true', help='Only report which steps pass')
    parser.add_argument('--cache', type=Path, action='append', default=[],
                        help='Folder of previously downloaded archives, reused only when the pinned hash matches')
    parser.add_argument('--compare-deps', type=Path, help='Existing dependency root to compare output hashes with')
    parser.add_argument('--reference-manifest', type=Path, help='Satchel.manifest.json of another IPA to compare with')
    args = parser.parse_args()
    work = Path(os.path.abspath(str(args.work.expanduser())))
    if work == ROOT or ROOT in work.parents or work in ROOT.parents:
        parser.error('The work folder must be outside the checkout')
    paths = layout(work)
    for name in ['home', 'tmp', 'logs', 'build-root', 'guard']:
        paths[name].mkdir(parents=True, exist_ok=True)
    if args.check:
        return check(paths, args.reference_manifest)
    selected = [name for name in args.steps.split(',') if name]
    unknown = set(selected) - set(STEPS)
    if unknown:
        parser.error('Unknown steps: ' + ', '.join(sorted(unknown)))
    caches = [str(Path(os.path.abspath(str(p.expanduser())))) for p in args.cache]
    python = paths['tools'] / 'bin/python3'
    for name in [s for s in STEPS if s in selected]:
        if name == 'tools':
            command = [sys.executable, '-B', ROOT / 'scripts/setup_tools.py', '--tools', paths['tools']]
            command += [x for c in caches for x in ['--cache', c]]
            code = step(paths, name, command, environment(paths))
        elif name == 'deps':
            if paths['deps'].exists():
                parser.error('The deps step needs a fresh root; move ' + str(paths['deps']) + ' away first')
            command = [python, '-B', ROOT / 'scripts/bootstrap_deps.py', '--root', paths['deps'],
                       '--tools', paths['tools'], '--fetch']
            command += [x for c in caches for x in ['--cache', c]]
            if args.compare_deps:
                command += ['--compare', args.compare_deps]
            code = step(paths, name, command, dict(environment(paths), ANYGAME_OUT=str(paths['guard'])))
        elif name == 'app':
            code = step(paths, name, [python, '-B', ROOT / 'NativePrepare/guard.py', python, '-B',
                                      ROOT / 'NativeApp/build.py', '--unsigned'], app_environment(paths))
        else:
            if paths['ipa'].exists():
                shutil.rmtree(str(paths['ipa']))
            code = step(paths, name, [python, '-B', ROOT / 'scripts/build_ipa.py', '--build-root', paths['out'],
                                      '--out', paths['ipa']], environment(paths))
        if code:
            print('Stopped at step ' + name + '; see ' + str(paths['logs'] / (name + '.log')), flush=True)
            check(paths, args.reference_manifest)
            return code
    return check(paths, args.reference_manifest)


if __name__ == '__main__':
    sys.exit(main())
