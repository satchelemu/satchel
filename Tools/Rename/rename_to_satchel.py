#!/usr/bin/env python3
"""Plan the clean-break tracked-tree rename, or write it with --apply."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
MATCH = re.compile('pocket', re.I)
RENAME = re.compile(r'Pocket[ _-]?Lab|POCKET[ _-]?LAB|pocket[ _-]?lab|Pocket|POCKET|pocket')
PL_POLICY = json.loads((HERE / 'pl-symbols.json').read_text())
PL_WORD = re.compile(r'(?<![A-Za-z0-9_])_?(?:PL[A-Z_][A-Za-z0-9_]*|pl_[A-Za-z0-9_]*)')
PL_FLAGS = re.compile(r'(?:(?<=-D)|(?<=-U))_?(?:PL[A-Z_][A-Za-z0-9_]*|pl_[A-Za-z0-9_]*)')
CONTROLLER = {'Tools/Rename/' + name for name in
              ['rename_to_satchel.py', 'test_rename.py', 'README.md', 'PHONE.md', 'pl-symbols.json']}


def excluded(name):
    parts = Path(name).parts
    return (name.startswith('Validation/history/') or name in CONTROLLER or
            any(part.lower() in {'upstream', 'vendor', 'vendored', 'third_party', 'third-party', 'notices', 'licenses'} for part in parts) or
            Path(name).name in {'THIRD_PARTY.md', 'THIRD-PARTY.md'} or
            bool(re.search(r'(?:^|/)(?:.*[-_])?(?:LICENSE|NOTICE|NOTICES|COPYING|OFL|PATENTS)(?:[.\-_]|$)', name, re.I)))


def replacement(match):
    word = match[0]
    return 'SATCHEL' if word.isupper() else 'satchel' if word.islower() else 'Satchel'


def replace_pl(match):
    word = match[0]
    prefix = '_' if word.startswith('_') else ''
    name = word[len(prefix):]
    return prefix + PL_POLICY['rename'].get(name, name)


def transform(text):
    text = RENAME.sub(replacement, text)
    text = PL_FLAGS.sub(replace_pl, text)
    return PL_WORD.sub(replace_pl, text)


NAME_POLICY = '''## Name

The app, project, targets, schemes and source identifiers use Satchel. The app ID is
`com.dell.satchel`; variants use the same prefix. Build and dependency folders are
`~/SatchelBuild` and `~/SatchelDeps`. The 2026-11-03 rename is a clean break: the user
backs up saves, removes the previous apps, installs the new app, imports credentials
and games again, and runs Prepare again. No old identity, ABI alias or data migration
is provided by the rename. Keep third-party notices and `Validation/history` intact.

'''
RENAME_PLAN = '''## Rename to Satchel (clean-break decision 2026-11-03)

Part 1 set the home-screen name. Part 2 uses `Tools/Rename/rename_to_satchel.py`
on the final merged main, with every worker stopped. It updates project, targets,
schemes, source identifiers, launch options, user-facing strings, namespaces,
bundle IDs, Keychain services, distribution metadata and file paths together.
The app ID is `com.dell.satchel`; all variants use that prefix. The repo folder
is already `satchel`.

Apply order: verify external save backups, merge worker changes, stop all workers,
preview and apply the tracked-tree script, relocate build/dependency folders with
the external mover, repair worktrees, rebuild every renamed native dependency,
then validate the host and unsigned Release app. The orchestrator signs and
installs only after those checks pass. Read `Tools/Rename/PHONE.md` for the exact
backup, uninstall, provisioning and restore procedure.

This is a fresh installation. The user imports keys, the signing identity and
games again and runs Prepare again. There are no old-name aliases, old ABI exports
or rename migration. History under `Validation/history` and third-party code and
notices remain unchanged. Prior milestone notes are historical evidence, not a
promise that renamed artifacts have been built or tested on the phone.
'''


def transform_file(name, text):
    # Rewrite policy before token replacement, so contradictory sentences cannot survive.
    if name == 'AGENTS.md':
        if '## Name\n' in text:
            text = re.sub(r'## Name\n.*?(?=^## |\Z)', lambda _: NAME_POLICY, text, flags=re.S | re.M)
        elif '## Direction\n' in text:
            text = text.replace('## Direction\n', NAME_POLICY + '## Direction\n', 1)
        else:
            raise ValueError('AGENTS.md name-policy anchor needs review')
    if name == 'GOALS.md':
        text = re.sub(r'- \[x\] Name:.*?(?=^- |^### |\Z)',
                      '- [x] Name: **Satchel**. The 2026-11-03 clean break uses `com.dell.satchel*`\n'
                      '  and Satchel project names. Back up saves before the fresh install; re-import\n'
                      '  credentials and games and run Prepare again. No rename migration.\n',
                      text, flags=re.S | re.M)
    if name == 'PLAN.md':
        text = re.sub(r'^\| Rename to Satchel, part 2:.*$',
                      '| Rename to Satchel, part 2 | orchestrator | tracked-tree names updated; external relocation, dependency rebuilds and phone reinstall require separate validation |',
                      text, flags=re.M)
        text = re.sub(r'## Rename to Satchel[^\n]*\n.*?(?=^## |\Z)', lambda _: RENAME_PLAN,
                      text, flags=re.S | re.M)
    result = transform(text)
    # Explicit roots make isolated validation possible without moving shared folders.
    # Defaults are only the new names; there is no old-path fallback.
    if name in {'NativeHost/build.py', 'NativeHost/services/build_mac.py',
                'NativeHost/services/run_mac.py', 'NativePrepare/build-dev.py'}:
        root_pattern = re.compile(r"Path\.home\(\)\s*/\s*(['\"])(SatchelBuild|SatchelDeps)(/[^'\"]*)?\1")
        def configured_root(match):
            variable = 'SATCHEL_BUILD_ROOT' if match[2] == 'SatchelBuild' else 'SATCHEL_DEPS_ROOT'
            base = "Path(os.environ.get(" + repr(variable) + ", Path.home().joinpath(" + repr(match[2]) + ")))"
            return base + (" / " + repr(match[3][1:]) if match[3] else '')
        result = root_pattern.sub(configured_root, result)
    if name == 'NativeHost/build.py' and 'default_deps = str(Path.home().joinpath(' not in result:
        before = "    def durable(s): return s.replace('/tmp/satchel-', str(snapshot / 'satchel-'), 1) if s.startswith('/tmp/satchel-') else s"
        after = """    def durable(s):
        default_deps = str(Path.home().joinpath('SatchelDeps'))
        configured_deps = str(Path(os.environ.get('SATCHEL_DEPS_ROOT', default_deps)))
        if s.startswith(default_deps + '/'):
            return configured_deps + s[len(default_deps):]
        return s.replace('/tmp/satchel-', str(snapshot / 'satchel-'), 1) if s.startswith('/tmp/satchel-') else s"""
        if result.count(before) != 1: raise ValueError('Dependency root anchor needs review')
        result = result.replace(before, after)
    if name == 'NativePrepare/build-core.zsh':
        anchor = 'mkdir -p "$TASK_OUT/core"'
        line = 'TASK_INPUT_ROOT="${SATCHEL_BUILD_ROOT:-$HOME/SatchelBuild}"'
        if line not in result:
            if result.count(anchor) != 1: raise ValueError('Prepare build root anchor needs review')
            result = result.replace(anchor, line + '\n' + anchor)
        result = result.replace('"$HOME/SatchelBuild/rewriter/capstone-src"', '"$TASK_INPUT_ROOT/rewriter/capstone-src"')
        result = result.replace('"$HOME/SatchelBuild/m5/archive-macosx/libShaderArchive.a"',
                                '"${SATCHEL_ARCHIVE_LIBRARY:-$TASK_INPUT_ROOT/m5/archive-macosx/libShaderArchive.a}"')
    if name in {'NativeShaders/build-translation.py', 'NativeShaders/check-pipeline.py',
                'NativeShaders/check-translator.py', 'NativeShaders/oracle.py', 'NativeShaders/guard.py'}:
        if 'import os\n' not in result:
            result = result.replace('from pathlib import Path\n', 'from pathlib import Path\nimport os\n')
        result = re.sub(r"Path\.home\(\)\s*/\s*'SatchelBuild/m5'",
                        "Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))", result)
        pattern = re.compile(r"Path\.home\(\)\s*/\s*(['\"])(SatchelBuild|SatchelDeps)(/[^'\"]*)?\1")
        def shader_root(match):
            variable = 'SATCHEL_BUILD_ROOT' if match[2] == 'SatchelBuild' else 'SATCHEL_DEPS_ROOT'
            base = "Path(os.environ.get(" + repr(variable) + ", Path.home().joinpath(" + repr(match[2]) + ")))"
            return base + (" / " + repr(match[3][1:]) if match[3] else '')
        result = pattern.sub(shader_root, result)
    if name == 'Tools/MaxwellShaderCompiler/build.py':
        result = result.replace('str(HERE / "NuGet.Config")',
                                'os.environ.get("SATCHEL_NUGET_CONFIG", str(HERE / "NuGet.Config"))') if 'SATCHEL_NUGET_CONFIG' not in result else result
    if name == 'Tools/CapturedFlatCompiler/build.py':
        result = result.replace('max_workers=4', 'max_workers=2')
        anchor = "[a.dotnet,'publish',root/'CapturedFlat.csproj',"
        if "'-p:IlcMaxDegreeOfParallelism=2'" not in result:
            if result.count(anchor) != 1: raise ValueError('Captured compiler parallelism anchor needs review')
            result = result.replace(anchor, anchor + "'-m:2','-p:IlcMaxDegreeOfParallelism=2',")
        staging = '''# Preserve pinned vendor bytes; rename only the project's budget call sites in scratch.
staged_upstream=out/'renamed-upstream';staged_hashes={};renamed_files=0;renamed_sites=0
for relative in sources:
 if not relative.startswith('upstream/'):continue
 data=(root/relative).read_bytes();count=0
 if relative.endswith('.cs'):
  data,count=re.subn(rb'\\b[A-Za-z_]\\w*Profile(?=\\.(?:WorkBudget|BoundKind)\\b)',b'SatchelProfile',data)
 target=staged_upstream/relative.removeprefix('upstream/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 staged_hashes[str(target.relative_to(staged_upstream))]=h(target)
 renamed_files+=bool(count);renamed_sites+=count
if (renamed_files,renamed_sites)!=(32,406):raise RuntimeError('Captured budget staging inventory changed')
(out/'staged-upstream.json').write_text(json.dumps({'files':staged_hashes,'renamed_files':renamed_files,'renamed_sites':renamed_sites},indent=2)+'\\n')
'''
        anchor = "renderer=check_lock(root/'renderer-reference-lock.json',a.renderer_root)\n"
        if 'staged_upstream=' not in result:
            if result.count(anchor) != 1: raise ValueError('Captured staging anchor needs review')
            result = result.replace(anchor, anchor + staging)
        result = result.replace("str(root/'upstream')", 'str(staged_upstream)')
        anchor = "artifacts={'host_dylib':"
        check = "for relative,expected in staged_hashes.items():\n if h(staged_upstream/relative)!=expected:raise RuntimeError('Staged upstream changed during build: '+relative)\n"
        if check not in result:
            if result.count(anchor) != 1: raise ValueError('Captured staging verification anchor needs review')
            result = result.replace(anchor, check + anchor)
        result = result.replace("'sources':sources,'source_lock_sha256'", "'sources':sources,'staged_sources':staged_hashes,'source_lock_sha256'")
    if name == 'Tools/CPUProbe/vixl-memory-guard.patch':
        # Recomputed from the pinned VIXL tree after applying only this renamed patch.
        # The builder still compares the entire git diff byte for byte.
        result = result.replace('index 0c842ab..8bd514e 100644', 'index 0c842ab..0c43bd2 100644')
        result = result.replace('index 5d6d975..260a54d 100644', 'index 5d6d975..42b446e 100644')
    if name in {'NativeShaders/build-cross.py', 'NativeShaders/build-archive.py'}:
        if 'import os\n' not in result:
            result = result.replace('from pathlib import Path\n', 'from pathlib import Path\nimport os\n')
        result = re.sub(r"Path\.home\(\)\s*/\s*'SatchelBuild/m5'",
                        "Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))", result)
    if name == 'NativePrepare/MachO.swift':
        # The exported marker grows by one byte, while its constant shrinks by two.
        # Keep the 40-byte export trie and the independent ld parity assertion.
        edits = {
            'section("__const", "__TEXT", 0x4000, 22, 0)':
                'section("__const", "__TEXT", 0x4000, 20, 0)',
            'exports.append(contentsOf: [33, 0, 0, 0, 0, 4, 0, 128, 128, 1, 0, 0])':
                'exports.append(contentsOf: [34, 0, 0, 0, 0, 4, 0, 128, 128, 1, 0])',
        }
        for before, after in edits.items():
            if after in result: continue
            if result.count(before) != 1:
                raise ValueError('Mach-O marker layout anchor needs review: ' + before)
            result = result.replace(before, after)
    if name in {'AGENTS.md', 'GOALS.md', 'PLAN.md'}:
        for phrase in ['bundle ID `com.dell.satchel` never changes', 'names stay `com.dell.satchel',
                       'Internal identifiers keep the old name', 'Keep: bundle ID']:
            if phrase in result:
                raise ValueError(name + ': conflicting rename policy needs review: ' + phrase)
    return result


def remaining(text):
    return [(text.count('\n', 0, m.start()) + 1, m[0]) for m in MATCH.finditer(text)]


def tracked(root):
    return subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode().split('\0')[:-1]


def safe_parent(root, path):
    return all(not parent.is_symlink() for parent in path.parents if parent != root and root in parent.parents)


def runtime_hashes(root):
    # NativePreparationRecipe.runtime verifies this exact repository-local binding.
    # Preserve the check: accept only the current declared digest before updating it.
    profiles = root / 'App/NativeRuntimeProfiles.json'
    registry = root / 'App/NativeGameProfiles.json'
    if not profiles.exists() and not registry.exists(): return {}, []
    if any(path.is_symlink() or not safe_parent(root, path) for path in (profiles, registry)):
        return {}, ['Runtime profile binding needs review: symlink input']
    try:
        values = json.loads(profiles.read_text())
        rows = json.loads(registry.read_text())
        edits = {}
        for row in rows:
            key = row['titleID'] + ':' + row['version']
            value = values[key]
            if not isinstance(value, str): raise ValueError('Runtime profile must be text: ' + key)
            before = hashlib.sha256(value.encode()).hexdigest()
            if row['profileSHA256'] != before:
                raise ValueError('Reviewed runtime profile digest mismatch: ' + key)
            after = hashlib.sha256(transform(value).encode()).hexdigest()
            if before != after: edits[before] = after
        return edits, []
    except (OSError, KeyError, TypeError, ValueError) as error:
        return {}, ['Runtime profile binding needs review: ' + str(error)]


def reviewed_hashes(root):
    edits, failures = {}, []
    for row in json.loads((HERE / 'reviewed-bindings.json').read_text()):
        source = root / transform(row['source'])
        if not source.exists(): source = root / row['source']
        if not (root / row['manifest']).exists(): continue
        if not source.exists() or source.is_symlink() or not safe_parent(root, source):
            failures.append('Reviewed source missing or symlink: ' + row['source'])
            continue
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if digest not in {row['before'], row['after']}:
            failures.append('Reviewed source changed, re-review binding: ' + row['source'])
            continue
        manifest = root / row['manifest']
        if manifest.is_symlink() or not safe_parent(root, manifest):
            failures.append('Reviewed manifest is a symlink: ' + row['manifest'])
            continue
        value = json.loads(manifest.read_text())
        for key in row['field']: value = value[key]
        if value not in {row['before'], row['after']}:
            failures.append('Reviewed manifest binding changed: ' + row['manifest'])
            continue
        edits.setdefault(row['manifest'], {})[row['before']] = row['after']
    return edits, failures


def plan(root):
    hash_edits, failures = runtime_hashes(root)
    binding_edits, binding_failures = reviewed_hashes(root)
    failures += binding_failures
    changes, inventory = [], Counter()
    destinations = {}
    for original in tracked(root):
        if excluded(original):
            inventory['excluded files'] += 1
            continue
        target = transform(original)
        if target in destinations and destinations[target] != original:
            failures.append(f'Collision: {original} and {destinations[target]} -> {target}')
        destinations[target] = original
        source = root / original
        # The index is deliberately untouched. A repeated run also works before git add.
        if not source.exists() and not source.is_symlink():
            source = root / target
        destination = root / target
        if not safe_parent(root, source) or not safe_parent(root, destination):
            failures.append(f'Symlink parent needs review: {original}')
            continue
        if not source.exists() and not source.is_symlink():
            failures.append(f'Missing tracked file: {original}')
            continue
        if source.is_dir():
            failures.append(f'Tracked submodule needs review: {original}')
            continue
        link = source.is_symlink()
        old = os.readlink(source).encode() if link else source.read_bytes()
        try:
            text = old.decode('utf-8')
            if '\0' in text: raise UnicodeError()
        except UnicodeError:
            if any(MATCH.search(old.decode(encoding, errors='ignore'))
                   for encoding in ('latin1', 'utf-16-le', 'utf-16-be', 'utf-32-le', 'utf-32-be')):
                failures.append(f'Binary needs review: {original}')
            if remaining(target): failures.append(f'Remaining path: {target}')
            if source != destination:
                if destination.exists() or destination.is_symlink():
                    failures.append(f'Occupied destination: {target}')
                changes.append((source, destination, old, old, False))
            continue
        for match in list(PL_WORD.finditer(text)) + list(PL_FLAGS.finditer(text)):
            if match[0].removeprefix('_') not in PL_POLICY['rename']:
                inventory['PL kept: ' + match[0]] += 1
        try:
            updated = transform(text) if link else transform_file(original, text)
            if original == 'App/NativeGameProfiles.json':
                for before, after in hash_edits.items():
                    updated, count = re.subn(r'("profileSHA256"\s*:\s*")' + before + '"',
                                             lambda m: m[1] + after + '"', updated)
                    if count == 0: raise ValueError('Runtime digest field anchor needs review')
            for before, after in binding_edits.get(original, {}).items():
                updated = updated.replace(before, after)
            new = updated.encode()
        except ValueError as error:
            failures.append(str(error))
            continue
        # Never break a link into immutable history, third-party code or another tree.
        if link and old != new:
            failures.append(f'Symlink target needs review: {original}')
        for line, token in remaining(new.decode()):
            failures.append(f'Remaining: {target}:{line}: {token}')
        if remaining(target): failures.append(f'Remaining path: {target}')
        if destination != source and (destination.exists() or destination.is_symlink()):
            failures.append(f'Occupied destination: {target}')
        if old != new or source != destination:
            changes.append((source, destination, old, new, link))
    return changes, failures, inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--apply', action='store_true')
    group.add_argument('--check', action='store_true', help='Fail if any rename remains pending')
    args = parser.parse_args()
    root = args.root.resolve()
    if Path(subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], text=True).strip()) != root:
        parser.error('Root must be the worktree top level.')
    changes, failures, inventory = plan(root)
    for source, target, old, new, link in changes:
        print(f'{source.relative_to(root)} -> {target.relative_to(root)}: '
              f'{len(MATCH.findall(old.decode("utf-8", errors="replace")))} old-name matches, '
              f'{"content + path" if source != target else "content"}')
    if args.check:
        for source, target, old, new, link in changes:
            if remaining(source.relative_to(root).as_posix()):
                print('REMAINING path: ' + source.relative_to(root).as_posix())
            for line, token in remaining(old.decode('utf-8', errors='replace')):
                print(f'REMAINING {source.relative_to(root)}:{line}: {token}')
    for reason, count in sorted(inventory.items()): print(f'ALLOW/REVIEW {count}: {reason}')
    for failure in failures: print(f'REVIEW {failure}')
    if failures:
        print(f'FAIL: {len(failures)} cases need review; no files written.')
        return 1
    if args.apply:
        # Check the whole plan again before writing, including destination collisions.
        for source, target, old, new, link in changes:
            if (os.readlink(source).encode() if link else source.read_bytes()) != old:
                raise RuntimeError(f'Concurrent change: {source.relative_to(root)}')
            if target != source and (target.exists() or target.is_symlink()):
                raise RuntimeError(f'Concurrent destination: {target.relative_to(root)}')
        for source, target, old, new, link in changes:
            target.parent.mkdir(parents=True, exist_ok=True)
            if link:
                target.symlink_to(new.decode())
                source.unlink()
            else:
                mode = source.stat().st_mode
                temporary = target.with_name(target.name + '.rename-tmp')
                with temporary.open('xb') as stream: stream.write(new)
                temporary.chmod(mode)
                os.replace(temporary, target)
                if source != target: source.unlink()
        for source, target, *_ in changes:
            parent = source.parent
            while parent != root and parent != target.parent:
                try: parent.rmdir()
                except OSError: break
                parent = parent.parent
        again, problems, _ = plan(root)
        if again or problems:
            print('FAIL: post-apply remaining-name or idempotence check', problems)
            return 1
    print(f'{"APPLIED" if args.apply else "CHECK" if args.check else "DRY RUN"}: '
          f'{len(changes)} files; 0 unallowed remaining matches in projected tree.')
    return int(args.check and bool(changes))


if __name__ == '__main__':
    sys.exit(main())
