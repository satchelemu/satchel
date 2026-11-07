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
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/Rename'))
from fetch_inputs import digest, obtain_pinned
from rebuild_dependencies import SNAPSHOT

LOCK = ROOT / 'scripts/deps/inputs.lock.json'
QUEUE = ROOT / 'scripts/deps/upstream/nxnative-queue'
PATCH_COUNT = 29
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
    """Apply the recorded patch series without creating or rewriting commits."""
    engine = root / 'NX2Apple/engine'
    published = root / 'sources/nx2apple-patches'
    pin = (QUEUE / '.upstream-ref').read_text().strip()
    head = subprocess.check_output(['git', '-C', engine, 'rev-parse', 'HEAD'], text=True).strip()
    if pin != head:
        raise RuntimeError('Engine patch queue and engine pin differ')
    helpers = runpy.run_path(str(published / 'tools/patches.py'))
    available = {patch.file for patch in helpers['load'](published)}
    names = [line.split('#', 1)[0].strip() for line in (QUEUE / 'series').read_text().splitlines()]
    names = [name for name in names if name]
    if len(names) != PATCH_COUNT or len(set(names)) != len(names):
        raise RuntimeError('Engine integration requires the recorded %d patches' % PATCH_COUNT)
    # A ported file replaces the published one. Every other patch must be published.
    ported = sorted(path.name for path in (QUEUE / 'ports').glob('*.patch'))
    if set(ported) - set(names):
        raise RuntimeError('Ported engine patch outside the series')
    files = []
    for name in names:
        if name in ported:
            files.append(QUEUE / 'ports' / name)
        elif name in available:
            files.append(published / 'patches' / name)
        else:
            raise RuntimeError('Engine patch is neither published nor ported: ' + name)
    for path in files:
        run(['git', '-C', engine, 'apply', path], capture_output=True)
    # Use the upstream generators, without its git-am/commit lifecycle.
    for name in ['docs_patches.py', 'docs_symptoms.py']:
        run([sys.executable, engine / 'dm/tools' / name], cwd=engine, capture_output=True)
    queue = [helpers['Patch'](path) for path in files]
    (engine / 'dm/fork_patches.txt').write_text(helpers['fork_patch_list'](queue))
    published_head = subprocess.check_output(['git', '-C', published, 'rev-parse', 'HEAD'], text=True).strip()
    write(root / 'NX2Apple/engine-materialization.json', {
        'pin': pin, 'patches': names, 'ported': ported, 'published_queue': published_head,
        'patch_sha256': {path.name: digest(path) for path in files},
        'method': 'git apply without index or commits; generated indexes via published tools'})


def load_tools(folder, lock):
    """Pinned tools made by scripts/setup_tools.py, checked against this lock."""
    folder = folder.expanduser().resolve()
    manifest = json.loads((folder / 'tools-manifest.json').read_text())
    for row in lock['tools']:
        if manifest['tools'].get(row['id'], {}).get('lock') != row:
            raise RuntimeError('Tool folder does not match the lock (run scripts/setup_tools.py again): ' + row['id'])
    wheel = folder / next(row['destination'] for row in lock['tools'] if row['id'] == 'fonttools')
    if digest(wheel) != next(row['sha256'] for row in lock['tools'] if row['id'] == 'fonttools'):
        raise RuntimeError('Pinned fontTools wheel mismatch')
    return {'root': folder, 'bin': folder / 'bin', 'dotnet': folder / 'dotnet/dotnet', 'fonttools': wheel,
            'python': folder / 'bin/python3'}


def make_venv(tools, venv):
    """A private environment from the pinned Python with only the pinned fontTools wheel."""
    run([tools['python'], '-I', '-m', 'venv', '--without-pip', venv])
    site = Path(subprocess.check_output([venv / 'bin/python3', '-I', '-c', 'import sysconfig; print(sysconfig.get_paths()["purelib"])'], text=True).strip())
    with zipfile.ZipFile(tools['fonttools']) as wheel:
        for member in wheel.infolist():
            target = (site / member.filename).resolve()
            if not inside(target, site.resolve()):
                raise RuntimeError('Wheel member outside site-packages: ' + member.filename)
        wheel.extractall(site)
    run([venv / 'bin/python3', '-I', '-c', 'import fontTools; assert fontTools.version == "4.68.0", fontTools.version'])


def prepare_app_inputs(root, source_cache, tools=None):
    """Generate public host metadata and fonts with the pinned tools, or explicit local diagnostic tools."""
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
    if tools:
        make_venv(tools, integration / 'venv')
    elif source_cache is None:
        raise RuntimeError('Local fontTools validation requires an explicit source cache')
    else:
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
    # Tools are staged and built by scripts/setup_tools.py, never by this script.
    return lock['inputs']


def recheck_staged(dest, lock, state):
    """The offline build half re-checks what the fetching half staged before using it."""
    if state.get('lock_sha256') != digest(LOCK) or state.get('steps'):
        raise RuntimeError('Staged root does not belong to this lock or was already built')
    for row in lock['inputs']:
        path = dest / row['destination']
        if state['inputs'].get(row['id'], {}).get('status') != 'verified':
            raise RuntimeError('Input was not verified while staging: ' + row['id'])
        if row['kind'] == 'git':
            head = subprocess.check_output(['git', '-C', path, 'rev-parse', 'HEAD'], text=True).strip()
            dirty = subprocess.check_output(['git', '-C', path, 'status', '--porcelain', '--untracked-files=all'], text=True)
            if head != row['version'] or dirty:
                raise RuntimeError('Staged Git input changed: ' + row['id'])
        elif digest(path) != row['sha256']:
            raise RuntimeError('Staged input changed: ' + row['id'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='New, empty dependency root outside this checkout')
    parser.add_argument('--tools', type=Path, help='Pinned tool folder from scripts/setup_tools.py (the clean route)')
    parser.add_argument('--cache', type=Path, action='append', default=[], help='Read-only archive cache; repeat for additional directories')
    parser.add_argument('--source-cache', type=Path, help='Read-only dependency source cache, never a default')
    parser.add_argument('--fetch', action='store_true', help='Allow pinned upstream downloads; recipes still build offline')
    parser.add_argument('--local-validation', action='store_true', help='Build available recipes with installed tools; never claims clean-Mac readiness')
    parser.add_argument('--dotnet', type=Path, help='Installed SDK executable for local validation only')
    parser.add_argument('--compare', type=Path, help='Read-only dependency root for output hash comparison')
    parser.add_argument('--inventory-only', action='store_true')
    parser.add_argument('--stage-only', action='store_true', help='Acquire only pinned inputs, retaining explicit gaps')
    parser.add_argument('--guarded', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--network-denied', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--staged', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    dest = args.root.expanduser().resolve()
    caches = [p.expanduser().resolve() for p in args.cache]
    source_cache = args.source_cache.expanduser().resolve() if args.source_cache else None
    reference = args.compare.expanduser().resolve() if args.compare else None
    tools_root = args.tools.expanduser().resolve() if args.tools else None
    for protected in [ROOT, *caches, *([source_cache] if source_cache else []), *([reference] if reference else []),
                      *([tools_root] if tools_root else [])]:
        if inside(dest, protected) or inside(protected, dest):
            parser.error('Output must be separate from repository, tools, caches and comparison root')
    if args.fetch and args.local_validation:
        parser.error('Local validation must remain offline')
    if args.dotnet and not args.local_validation:
        parser.error('Installed SDK overrides require --local-validation')
    if args.tools and (args.local_validation or source_cache):
        parser.error('The pinned-tool route uses neither installed tools nor a source cache')
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
    if not args.local_validation and not args.stage_only and not args.tools:
        parser.error('Without --local-validation the build needs --tools from scripts/setup_tools.py')
    tools = load_tools(tools_root, lock) if tools_root else None
    if not args.guarded:
        env = dict(os.environ, ANYGAME_OUT=str(dest.parent / 'bootstrap-guard'))
        return subprocess.call([sys.executable, '-B', ROOT / 'NativePrepare/guard.py', sys.executable,
                                '-B', __file__, *sys.argv[1:], '--guarded'], env=env)
    if not args.fetch and not args.network_denied:
        # Deny outbound IP traffic to descendants; local MSBuild IPC must work.
        return subprocess.call(['/usr/bin/sandbox-exec', '-p', OFFLINE_POLICY,
                                sys.executable, '-B', __file__, *sys.argv[1:], '--network-denied'])
    logs = dest / 'bootstrap-logs'
    if args.staged:
        state = json.loads((dest / 'bootstrap-manifest.json').read_text())
        recheck_staged(dest, lock, state)
    else:
        if dest.exists() and any(dest.iterdir()):
            parser.error('Use a fresh empty root; partial roots are retained for diagnosis')
        dest.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(dest).free < 36 * GIB:
            raise RuntimeError('Disk reserve: need 36 GiB before starting')
        state = {'clean_mac_ready': False, 'lock_sha256': digest(LOCK), 'inputs': {},
                 'route': 'pinned tools from scripts/setup_tools.py' if tools else 'installed tools (local validation)',
                 'gaps': lock['gaps'], 'tool_recipe_gaps': lock.get('tool_recipe_gaps', []),
                 'steps': {}, 'network': 'staging only (pinned URLs); recipes built with outbound IP denied by sandbox-exec' if args.fetch else 'outbound IP denied by sandbox-exec; local IPC allowed'}
        if tools:
            state['tools'] = {row['id']: row['version'] for row in lock['tools']}
        logs.mkdir()
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
                    write(dest / 'bootstrap-manifest.json', state)
                    raise
        write(dest / 'bootstrap-manifest.json', state)
        if args.stage_only:
            return 2 if lock['gaps'] or lock.get('tool_recipe_gaps') or any(row['status'] != 'verified' for row in state['inputs'].values()) else 0
        if args.fetch:
            # Every recipe runs offline: the fetching half ends here and hands the staged root over.
            argv = [a for a in sys.argv[1:] if a != '--fetch']
            return subprocess.call(['/usr/bin/sandbox-exec', '-p', OFFLINE_POLICY, sys.executable, '-B', __file__,
                                    *argv, '--staged', '--network-denied'])
    env = dict(os.environ, SATCHEL_DEPS_ROOT=str(dest), SATCHEL_SHADER_DEPS=str(dest / 'm5'),
               PYTHONDONTWRITEBYTECODE='1', NUGET_CERT_REVOCATION_MODE='offline', ARCHFLAGS='-arch arm64',
               DOTNET_CLI_HOME=str(dest / 'cli'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               DOTNET_GENERATE_ASPNET_CERTIFICATE='false', DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
    env.pop('SATCHEL_BUILD_SLOT', None)
    if tools:
        env['PATH'] = str(tools['bin']) + os.pathsep + env.get('PATH', '/usr/bin:/bin')

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
        sdk = tools['dotnet'] if tools else args.dotnet.expanduser().resolve() if args.dotnet else None
        if sdk:
            integration = dest / SNAPSHOT / 'satchel-switch-integration'
            # A pinned SDK (tools) or an explicit diagnostic installed SDK (local validation).
            run(['cp', '-cR', sdk.parent, integration / 'dotnet'])
            dotnet = integration / 'dotnet/dotnet'
            state['sdk' if tools else 'local_sdk'] = {
                'version': subprocess.check_output([dotnet, '--version'], env=env, text=True).strip(),
                'executable_sha256': digest(dotnet), 'archive_pin_missing': not tools}
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
            prepare_app_inputs(dest, source_cache, tools)
            state['steps']['app-inputs'] = 0
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            state['steps']['app-inputs'] = str(error)
        execute('public-shader-parity', [sys.executable, '-B', ROOT / 'NativeShaders/public_parity.py',
                '--products', dest / 'm5', '--output', dest / 'm5/public-parity'])
    finally:
        state['outputs'] = output_manifest(dest, reference)
        complete = (tools is not None and not lock['gaps'] and not lock.get('tool_recipe_gaps')
                    and all(row.get('status') == 'verified' for row in state['inputs'].values())
                    and len(state['inputs']) == len(lock['inputs'])
                    and all(code == 0 for code in state['steps'].values()) and not state['outputs']['missing'])
        state['clean_mac_ready'] = complete
        write(dest / 'bootstrap-manifest.json', state)
    if complete:
        print('Dependency root complete from pinned inputs and pinned tools: ' + str(len(state['outputs']['files'])) + ' output files, none missing.', flush=True)
        return 0
    print('Dependency validation recorded but incomplete; see bootstrap-manifest.json.' if tools else
          'Local dependency validation recorded. Clean-Mac bootstrap remains blocked; see bootstrap-manifest.json.', flush=True)
    return 2


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
