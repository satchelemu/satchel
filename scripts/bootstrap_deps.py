#!/usr/bin/env python3
"""Stage pinned inputs and reuse the native dependency recipes, without game inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import runpy
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/Rename'))
from fetch_inputs import digest, obtain_pinned
from rebuild_dependencies import SNAPSHOT

LOCK = ROOT / 'scripts/deps/inputs.lock.json'
GIB = 1024 ** 3
OFFLINE_POLICY = '(version 1) (allow default) (deny network-outbound (remote ip "*:*"))'


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def git_digest(source, revision):
    child = subprocess.Popen(['git', '-C', str(source), 'archive', '--format=tar', revision], stdout=subprocess.PIPE)
    value = hashlib.file_digest(child.stdout, 'sha256').hexdigest()
    child.stdout.close()
    if child.wait():
        raise RuntimeError('Cannot archive pinned Git input')
    return value


def inside(path, parent):
    return path == parent or parent in path.parents


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        source.extractall(destination, filter='data')


def stage_git(row, destination, caches, source_cache, fetch):
    source = None
    if source_cache and row.get('source_cache'):
        candidate = source_cache / row['source_cache']
        if candidate.is_dir():
            source = candidate
    if source is None and row.get('cache'):
        source = next((p / row['cache'] for p in caches if (p / row['cache']).is_dir()), None)
    if source is None and not fetch:
        raise RuntimeError('Missing offline input: ' + row['id'])
    if destination.exists():
        raise RuntimeError('Source destination already exists: ' + row['id'])
    destination.parent.mkdir(parents=True, exist_ok=True)
    run(['git', 'init', '-q', destination])
    # A local fetch copies only the pinned objects. Never copy dirty worktrees,
    # ignored files, prebuilt libraries, Git hooks or another checkout's config.
    run(['git', '-c', 'protocol.file.allow=always', '-C', destination, 'fetch', '-q', '--depth=1',
         str(source) if source else row['url'], row['version']])
    if git_digest(destination, row['version']) != row['sha256']:
        raise RuntimeError('Pinned SHA-256 mismatch: ' + row['id'])
    run(['git', '-C', destination, 'checkout', '-q', '--detach', row['version']])


def materialize_engine(root):
    """Apply the pinned upstream queue without creating or rewriting commits."""
    engine = root / 'NX2Apple/engine'
    patches = root / 'sources/nx2apple-patches/patches'
    pin = (patches.parent / '.upstream-ref').read_text().strip()
    head = subprocess.check_output(['git', '-C', engine, 'rev-parse', 'HEAD'], text=True).strip()
    if pin != head:
        raise RuntimeError('Engine patch queue and engine pin differ')
    helpers = runpy.run_path(str(patches.parent / 'tools/patches.py'))
    queue = helpers['load'](patches.parent)
    names = [patch.file for patch in queue]
    if len(names) != 34:
        raise RuntimeError('Engine integration requires the recorded 34 patches')
    for name in names:
        run(['git', '-C', engine, 'apply', patches / name], capture_output=True)
    # Use the upstream generators, without its git-am/commit lifecycle.
    for name in ['docs_patches.py', 'docs_symptoms.py']:
        run([sys.executable, engine / 'dm/tools' / name], cwd=engine, capture_output=True)
    (engine / 'dm/fork_patches.txt').write_text(helpers['fork_patch_list'](queue))
    write(root / 'NX2Apple/engine-materialization.json', {
        'pin': pin, 'patches': names,
        'patch_sha256': {name: digest(patches / name) for name in names},
        'method': 'git apply without index or commits; generated indexes via published tools'})


def prepare_app_inputs(root, source_cache):
    """Generate public host metadata and fonts with explicit local diagnostic tools."""
    inputs = root / 'build-inputs'
    inputs.mkdir(exist_ok=True)
    shutil.copyfile(root / 'NX2Apple/engine-materialization.json', inputs / 'engine-materialization.json')
    write(inputs / 'build-state.json', {'stage': 'bootstrap'})
    metal = Path(subprocess.check_output(['xcrun', '--find', 'metal'], text=True).strip())
    toolchain = next((p for p in metal.parents if p.suffix == '.xctoolchain'), None)
    if toolchain is None:
        raise RuntimeError('Cannot identify the selected Xcode Metal toolchain')
    write(inputs / 'native-toolchain.json', {'metal_toolchain': str(toolchain)})
    (inputs / 'profiles').symlink_to(root / 'NX2Apple/engine/dm/games', target_is_directory=True)
    integration = root / SNAPSHOT / 'satchel-switch-integration'
    if source_cache is None:
        raise RuntimeError('Local fontTools validation requires an explicit source cache')
    # Diagnostic reuse is explicit. It does not resolve the fontTools distribution pin.
    run(['cp', '-cR', source_cache / SNAPSHOT / 'satchel-switch-integration/venv', integration / 'venv'])
    fonts = inputs / 'fonts'
    fonts.mkdir()
    font = root / 'inputs/NotoSans-Regular.ttf'
    if digest(font) != '478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823':
        raise RuntimeError('Pinned Noto font mismatch')
    shutil.copyfile(font, fonts / 'standard.ttf')
    run([integration / 'venv/bin/python3', root / 'NX2Apple/engine/dm/tools/make_ext_font.py', fonts / 'nintendo_ext.ttf'],
        env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'), capture_output=True)


def output_manifest(root, reference):
    names = json.loads((ROOT / 'Tools/Rename/dependency-artifacts.json').read_text())
    names += ['m5/cross-iphoneos/libGameShaderBridge.a', 'm5/cross-macosx/libGameShaderBridge.a',
              'm5/archive-iphoneos/libShaderArchive.a', 'm5/archive-macosx/libShaderArchive.a',
              'm5/aot/SatchelShaderProbe.framework', 'openssl-ios/lib/libcrypto.a',
              'rewriter-ios/_deps/capstone-build/libcapstone.a', 'rewriter-ios/libnative_rewriter.a']
    result = {'files': {}, 'missing': []}
    for name in names:
        path = root / name
        if not path.exists():
            result['missing'].append(name)
            continue
        for file in sorted(path.rglob('*')) if path.is_dir() else [path]:
            if not file.is_file():
                continue
            relative = file.relative_to(root).as_posix()
            entry = {'sha256': digest(file), 'bytes': file.stat().st_size}
            if reference:
                other = reference / relative
                entry['reference_sha256'] = digest(other) if other.is_file() else None
                entry['comparison'] = 'identical' if entry['reference_sha256'] == entry['sha256'] else 'different' if other.is_file() else 'reference absent'
            result['files'][relative] = entry
    write(root / 'output-manifest.json', result)
    return result


def staging_inputs(lock, stage_only):
    # Tool sources are fetched only for explicit input staging. Local validation
    # continues to use installed tools and must not imply source-build coverage.
    return lock['inputs'] + (lock.get('tool_sources', []) if stage_only else [])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='New, empty dependency root outside this checkout')
    parser.add_argument('--cache', type=Path, action='append', default=[], help='Read-only archive cache; repeat for additional directories')
    parser.add_argument('--source-cache', type=Path, help='Read-only dependency source cache, never a default')
    parser.add_argument('--fetch', action='store_true', help='Allow pinned upstream downloads; default is offline')
    parser.add_argument('--local-validation', action='store_true', help='Build available recipes with installed tools; never claims clean-Mac readiness')
    parser.add_argument('--dotnet', type=Path, help='Installed SDK executable for local validation only')
    parser.add_argument('--compare', type=Path, help='Read-only dependency root for output hash comparison')
    parser.add_argument('--inventory-only', action='store_true')
    parser.add_argument('--stage-only', action='store_true', help='Acquire only pinned inputs, retaining explicit gaps')
    parser.add_argument('--guarded', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--network-denied', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    dest = args.root.expanduser().resolve()
    caches = [p.expanduser().resolve() for p in args.cache]
    source_cache = args.source_cache.expanduser().resolve() if args.source_cache else None
    reference = args.compare.expanduser().resolve() if args.compare else None
    for protected in [ROOT, *caches, *([source_cache] if source_cache else []), *([reference] if reference else [])]:
        if inside(dest, protected) or inside(protected, dest):
            parser.error('Output must be separate from repository, caches and comparison root')
    if args.fetch and args.local_validation:
        parser.error('Local validation must remain offline')
    if args.dotnet and not args.local_validation:
        parser.error('Installed SDK overrides require --local-validation')
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('This build route requires an Apple Silicon Mac')
    lock = json.loads(LOCK.read_text())
    if args.inventory_only:
        print(json.dumps(lock, indent=2))
        return 0
    if not args.local_validation and not args.stage_only and (lock['gaps'] or lock.get('tool_recipe_gaps')):
        print('Clean-Mac bootstrap blocked by unpinned inputs: ' + ', '.join(row['id'] for row in lock['gaps']))
        print('Unvalidated tool source recipes: ' + ', '.join(row['id'] for row in lock.get('tool_recipe_gaps', [])))
        return 2
    if not args.guarded:
        env = dict(os.environ, ANYGAME_OUT=str(dest.parent / 'bootstrap-guard'))
        return subprocess.call([sys.executable, '-B', ROOT / 'NativePrepare/guard.py', sys.executable,
                                '-B', __file__, *sys.argv[1:], '--guarded'], env=env)
    if not args.fetch and not args.network_denied:
        # Deny outbound IP traffic to descendants; local MSBuild IPC must work.
        return subprocess.call(['/usr/bin/sandbox-exec', '-p', OFFLINE_POLICY,
                                sys.executable, '-B', __file__, *sys.argv[1:], '--network-denied'])
    if dest.exists() and any(dest.iterdir()):
        parser.error('Use a fresh empty root; partial roots are retained for diagnosis')
    dest.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(dest).free < 36 * GIB:
        raise RuntimeError('Disk reserve: need 36 GiB before starting')
    state = {'clean_mac_ready': False, 'lock_sha256': digest(LOCK), 'inputs': {},
             'gaps': lock['gaps'], 'tool_recipe_gaps': lock.get('tool_recipe_gaps', []),
             'tool_sources': lock.get('tool_sources', []), 'steps': {}, 'network': 'allowed' if args.fetch else 'outbound IP denied by sandbox-exec; local IPC allowed'}
    logs = dest / 'bootstrap-logs'
    logs.mkdir()
    env = dict(os.environ, SATCHEL_DEPS_ROOT=str(dest), SATCHEL_SHADER_DEPS=str(dest / 'm5'),
               PYTHONDONTWRITEBYTECODE='1', NUGET_CERT_REVOCATION_MODE='offline', ARCHFLAGS='-arch arm64',
               DOTNET_CLI_HOME=str(dest / 'cli'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               DOTNET_GENERATE_ASPNET_CERTIFICATE='false', DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
    env.pop('SATCHEL_BUILD_SLOT', None)

    def execute(label, command):
        print('Building ' + label, flush=True)
        with (logs / (label + '.log')).open('w') as stream:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        state['steps'][label] = result.returncode
        write(dest / 'bootstrap-manifest.json', state)
        if result.returncode:
            print('Failed ' + label + ': exit ' + str(result.returncode) + ', see bootstrap-logs/' + label + '.log', flush=True)
        return result.returncode == 0

    try:
        for row in staging_inputs(lock, args.stage_only):
            path = dest / row['destination']
            try:
                if row['kind'] == 'git':
                    stage_git(row, path, caches, source_cache, args.fetch)
                else:
                    cached = next((p / name for p in caches for name in [row['cache'], *row.get('cache_aliases', [])] if (p / name).is_file()), None)
                    obtain_pinned(path, row['url'], row['sha256'], cached, args.fetch)
                state['inputs'][row['id']] = {'sha256': row['sha256'], 'status': 'verified'}
            except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
                state['inputs'][row['id']] = {'status': 'missing or invalid', 'error': str(error)}
                print(str(error), flush=True)
                if not args.local_validation:
                    raise
        write(dest / 'bootstrap-manifest.json', state)
        if args.stage_only:
            return 2 if lock['gaps'] or lock.get('tool_recipe_gaps') or any(row['status'] != 'verified' for row in state['inputs'].values()) else 0
        materialize_engine(dest)
        # Existing recipes retain their ABI checks and synthetic execution gates.
        bridge = dest / SNAPSHOT / 'satchel-shader-bridge'
        bridge.mkdir(parents=True, exist_ok=True)
        if (dest / 'm5/spirv-cross').exists():
            (bridge / 'spirv-cross').symlink_to(dest / 'm5/spirv-cross', target_is_directory=True)
        base = [sys.executable, '-B', ROOT / 'Tools/Rename/rebuild_dependencies.py', '--source', ROOT,
                '--destination', dest, '--prepared-inputs', '--apply']
        for step in ['cpu', 'archives', 'scalar', 'cross', 'archive', 'shader-bridge']:
            execute(step, [*base, '--step', step])
        # The legacy fetcher also verifies package signatures and performs offline
        # extraction. Its file pins now come from the tracked bootstrap lock.
        mvk = dest / 'inputs/moltenvk'
        mvk.mkdir(parents=True, exist_ok=True)
        source = dest / SNAPSHOT / 'satchel-vulkan-backend/MoltenVK-source'
        if source.exists():
            (mvk / 'MoltenVK-source').symlink_to(source, target_is_directory=True)
        if args.dotnet:
            dotnet = args.dotnet.expanduser().resolve()
            integration = dest / SNAPSHOT / 'satchel-switch-integration'
            # Installed SDK is an explicit diagnostic input, not a reproducibility pin.
            run(['cp', '-cR', dotnet.parent, integration / 'dotnet'])
            dotnet = integration / 'dotnet/dotnet'
            state['local_sdk'] = {'version': subprocess.check_output([dotnet, '--version'], env=env, text=True).strip(),
                                  'executable_sha256': digest(dotnet), 'archive_pin_missing': True}
            if execute('offline-packages', [sys.executable, '-B', ROOT / 'Tools/Rename/fetch_inputs.py',
                       '--source', ROOT, '--inputs', dest / 'inputs', '--dotnet', dotnet]):
                for framework in ['fixture-aot', 'game-aot', 'captured', 'vulkan']:
                    execute('framework-' + framework, [*base, '--step', 'frameworks', '--framework', framework,
                            '--nativeaot-packages', dest / 'inputs/packages', '--moltenvk-inputs', mvk])
        else:
            state['steps']['frameworks'] = 'blocked: missing explicit local SDK'
        # Stage only source archives, never an installed libcrypto or Capstone library.
        for name in ['capstone.tar.gz', 'openssl-3.6.3.tar.gz']:
            path = dest / 'inputs' / name
            if path.exists():
                extract(path, dest / 'sources')
        capstone = dest / 'sources/capstone-d2e5739cbb43df93d589a3a726c8f878b95beccb'
        if capstone.exists():
            configured = execute('capstone-configure', ['cmake', '-S', ROOT / 'Tools/NativeRewriter', '-B', dest / 'rewriter-ios',
                '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_SYSTEM_PROCESSOR=arm64', '-DCMAKE_OSX_SYSROOT=iphoneos',
                '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0', '-DCMAKE_BUILD_TYPE=Release',
                '-DNR_BUILD_SHARED=OFF', '-DBUILD_TESTING=OFF', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
                '-DFETCHCONTENT_SOURCE_DIR_CAPSTONE=' + str(capstone)])
            if configured:
                execute('capstone-build', ['cmake', '--build', dest / 'rewriter-ios', '-j2'])
        ssl = dest / 'sources/openssl-3.6.3'
        if ssl.exists():
            sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
            env['CROSS_TOP'] = str(Path(sdk).parent.parent)
            env['CROSS_SDK'] = Path(sdk).name
            execute('openssl', ['/bin/sh', '-c', 'cd "$1" && ./Configure ios64-xcrun no-shared no-tests no-apps no-module -miphoneos-version-min=17.0 --prefix="$2" && make -j2 build_libs && make -j2 install_dev', 'bootstrap-openssl', ssl, dest / 'openssl-ios'])
        try:
            prepare_app_inputs(dest, source_cache)
            state['steps']['app-inputs'] = 0
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            state['steps']['app-inputs'] = str(error)
        state['steps']['app-prerequisite'] = 'blocked: shader staging requires private pipeline-full parity proof'
        # Never fabricate pipeline-full/result.json or bypass its assertion.
    finally:
        state['outputs'] = output_manifest(dest, reference)
        write(dest / 'bootstrap-manifest.json', state)
    print('Local dependency validation recorded. Clean-Mac bootstrap remains blocked; see bootstrap-manifest.json.', flush=True)
    return 2


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
