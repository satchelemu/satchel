#!/usr/bin/env python3
"""Build-only M3.5 driver. Products and source snapshots stay outside the repository."""
from pathlib import Path
import argparse, hashlib, json, os, re, runpy, shlex, shutil, subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('SATCHEL_HOST_OUT', str(Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'host')))
ENGINE = OUT / 'engine'
ZELDA = DEPENDENCY_PATHS['build_inputs']()
TOOLS = DEPENDENCY_PATHS['deps_root']() / 'tmp-snapshot-2026-11-01/tmp/satchel-switch-integration'
GAME = '0100F2C0115B6000'
PIN = 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
CHAIN = '26dc2fa9477c'
PATCHES = 29

def quoted(path):
    # Xcode splits unquoted build-setting values on spaces; the checkout lives under "Repo - Personal".
    return f'"{path}"' if ' ' in path and not path.startswith('"') else path


def run(args, **kw):
    subprocess.run([str(x) for x in args], check=True, **kw)

def heavy_guard():
    state = json.loads((ZELDA / 'build-state.json').read_text())
    runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
    if shutil.disk_usage(OUT).free < 35 * 1024**3:
        raise SystemExit('Heavy build blocked: less than 35 GiB free')
    print('Heavy build guard: no Zelda build process; state=' + state['stage'], flush=True)

def prepare(unsigned=False):
    source = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
    material = json.loads((ZELDA / 'engine-materialization.json').read_text())
    assert material['pin'] == PIN and len(material['patches']) == PATCHES, (
        f"{ZELDA / 'engine-materialization.json'} records engine {material['pin'][:8]} with {len(material['patches'])} patches, "
        f'not {PIN[:8]} with {PATCHES}: set SATCHEL_NATIVE_BUILD_INPUTS (default <SATCHEL_DEPS_ROOT>/build-inputs) '
        'to the build inputs of a dependency root at the pin; see BUILDING.md')
    assert subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip() == PIN
    if not ENGINE.exists():
        shutil.copytree(source, ENGINE, ignore=shutil.ignore_patterns('.git', 'gen', '__pycache__', '.DS_Store'))
    # Verify copied runtime sources against the materialized fork before building.
    hashes = {}
    for base in ['host', 'gfx', 'third_party', 'ios/Sources']:
        for p in (source / 'dm' / base).rglob('*'):
            if p.is_file() and 'gen' not in p.relative_to(source).parts and '__pycache__' not in p.parts:
                q = ENGINE / p.relative_to(source)
                assert q.is_file() and hashlib.sha256(p.read_bytes()).digest() == hashlib.sha256(q.read_bytes()).digest(), str(p)
                hashes[str(p.relative_to(source))] = hashlib.sha256(q.read_bytes()).hexdigest()
    (OUT / 'engine-source-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
    tool_env = runpy.run_path(str(ROOT / 'scripts/switch_native.py'))['tool_environment'](TOOLS)
    tool_env['SATCHEL_METAL_TOOLCHAIN'] = json.loads((ZELDA / 'native-toolchain.json').read_text())['metal_toolchain']
    (OUT / 'tmp').mkdir(exist_ok=True)
    tool_env['TMPDIR'] = str(OUT / 'tmp')
    env = dict(tool_env, GAME=GAME, DM_GAME=GAME, DM_GAMES_FIRST='1',
               DM_GAMES_DIR=str(ZELDA / 'profiles'), PYTHONDONTWRITEBYTECODE='1')
    gen = ENGINE / 'dm/host/gen'
    gen.mkdir(exist_ok=True)
    (gen / GAME).mkdir(exist_ok=True)
    run(['python3', ENGINE / 'dm/games/game.py', 'header', GAME, gen / GAME / 'game_profile.h'], env=env)
    # Resolve the installed Makefile without running its compile recipes.
    cmds = subprocess.check_output(['make', '-s', '-n', '-B', '-C', ENGINE / 'dm/host', 'dmhost', 'MSL_SDK=iphoneos'], env=env, text=True)
    lines = [shlex.split(l) for l in cmds.splitlines() if l.lstrip().startswith('clang')]
    lines.sort(key=lambda t: 'dmhost' not in t)
    incs, defs, frameworks, src = [], [], [], []
    for tokens in lines:
        for i, token in enumerate(tokens):
            if token.startswith('-I'):
                incs.append(str((ENGINE / 'dm/host' / token[2:]).resolve()))
            elif token.startswith('-D'): defs.append(token)
            elif token == '-framework' and tokens[i+1] != 'Cocoa': frameworks.append(tokens[i+1])
            elif token.endswith(('.c', '.m', '.S')):
                p = (ENGINE / 'dm/host' / token).resolve()
                incs.append(str(p.parent))
                if not p.stem.endswith('_mac') and 'ios/Sources' not in str(p): src.append(p)
    incs.extend([str(ENGINE / 'dm/host'), str(ENGINE / 'dm/ios/Sources'), str(ROOT / 'NativeHost')])
    # SaveSync.m moves the cloud save file through NSFileCoordinator (iCloud downloads, no torn writes).
    src += [ENGINE / 'dm/ios/Sources' / n for n in ['front_ios.m', 'DmGamepad.m', 'SaveSync.m']]
    groups = {'SatchelNativeHost': [], 'SatchelNativeGraphics': [], 'SatchelNativeMedia': []}
    for p in dict.fromkeys(src):
        group = 'SatchelNativeMedia' if '/third_party/' in str(p) else 'SatchelNativeGraphics' if '/gfx/' in str(p) else 'SatchelNativeHost'
        row = {'path': str(p)}
        if p == ENGINE / 'dm/host/main.c': row['compilerFlags'] = '-Dmain=dm_main'
        groups[group].append(row)
    overlays = OUT / 'overlays'; overlays.mkdir(exist_ok=True)
    # Session diagnostics must start after the package environment is set.
    memstat = (ENGINE / 'dm/host/memstat.m').read_text()
    before = '__attribute__((constructor)) static void memstat_start(void)'
    assert memstat.count(before) == 1
    memstat = memstat.replace(before, 'void satchel_memstat_start(void)')
    (overlays / 'memstat.m').write_text(memstat)
    # Crash diagnostics retain signal, symbolic offsets and backtraces, without raw
    # register-target or stack bytes from the private guest image.
    crash = (ENGINE / 'dm/host/crash.c').read_text()
    first = crash.index('    for (int i = 0; i < 29; i++) fprintf(stderr, "x%d=')
    last = crash.index('    dm_backtrace(', first)
    crash = crash[:first] + crash[last:]
    first = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp;   // The frame just popped')
    last = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp; int n = 0;', first)
    crash = crash[:first] + crash[last:]
    crash = runpy.run_path(str(ROOT / 'NativeHost/session/crash_overlay.py'))['safe_stack_scan'](crash)
    (overlays / 'crash.c').write_text(crash)
    for row in groups['SatchelNativeHost']:
        if Path(row['path']).name in ('memstat.m', 'crash.c'): row['path'] = str(overlays / Path(row['path']).name)
    groups['SatchelNativeHost'].append({'path': str(ROOT / 'NativeHost/SessionBridge.m')})
    runpy.run_path(str(ROOT / 'NativeHost/display_overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    runpy.run_path(str(ROOT / 'NativeHost/session/overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    runpy.run_path(str(ROOT / 'NativeHost/graphics/overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    runpy.run_path(str(ROOT / 'NativeHost/memory/overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    runpy.run_path(str(ROOT / 'NativeHost/services/overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    runpy.run_path(str(ROOT / 'NativeHost/controllers/overlay.py'))['apply'](ROOT, ENGINE, OUT, groups)
    base = json.loads(subprocess.check_output(['ruby', '-ryaml', '-rjson', '-e', 'puts YAML.load_file(ARGV[0]).to_json', ROOT / 'project.yml'], text=True))
    target = base['targets']['Satchel']
    for row in target['sources']:
        if row['path'] == 'App': row['excludes'] = ['Info.plist']
        row['path'] = str(ROOT / row['path'])
    target['sources'].append({'path': str(ROOT / 'NativeHost/PreparedGame.swift')})
    target['sources'].append({'path': str(ROOT / 'NativeHost/PlayerSettingsView.swift')})
    target['sources'].append({'path': str(ROOT / 'NativeHost/PlayerProfiles.plist'), 'buildPhase': 'resources'})
    target['sources'].append({'path': str(ROOT / 'NativeHost/NativeMenuBridge.swift')})
    target['sources'].append({'path': str(ROOT / 'NativeHost/SatchelMenuSession.swift')})
    target['sources'].append({'path': str(OUT / 'NativeAssets'), 'type': 'folder', 'buildPhase': 'resources'})
    snapshot = DEPENDENCY_PATHS['deps_root']() / 'tmp-snapshot-2026-11-01/tmp'
    def durable(s):
        default_deps = str(Path.home().joinpath('SatchelDeps'))
        configured_deps = str(DEPENDENCY_PATHS['deps_root']())
        if s.startswith(default_deps + '/'):
            return configured_deps + s[len(default_deps):]
        # project.yml spells library paths under the maintainer's home folder. On any other
        # account (or with HOME isolated) they must still move to the configured root, never
        # resolve to whatever happens to exist at the tracked spelling.
        tracked = re.match(r'/Users/[^/]+/SatchelDeps(?=/)', s)
        if tracked:
            return configured_deps + s[tracked.end():]
        return s.replace('/tmp/satchel-', str(snapshot / 'satchel-'), 1) if s.startswith('/tmp/satchel-') else s
    for dep in target['dependencies']:
        if 'framework' in dep:
            dep['framework'] = durable(dep['framework'])
            assert Path(dep['framework']).exists(), 'Missing durable dependency: ' + dep['framework']
    settings = target['settings']['base']
    settings.pop('DEVELOPMENT_TEAM', None)
    team = None if unsigned else runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['signing_team'](ROOT, required=False)
    if team: settings['DEVELOPMENT_TEAM'] = team
    settings.update(PRODUCT_BUNDLE_IDENTIFIER='com.dell.satchel.dev', PRODUCT_NAME='SatchelDev',
        ASSETCATALOG_COMPILER_APPICON_NAME='AppIconDev',
        SWIFT_OBJC_BRIDGING_HEADER=str(ROOT / 'NativeHost/Bridge.h'),
        SWIFT_ACTIVE_COMPILATION_CONDITIONS='$(inherited)',
        CODE_SIGN_ENTITLEMENTS=str(ROOT / 'NativeHost/Dev.entitlements'),
        IPHONEOS_DEPLOYMENT_TARGET='26.0', DEAD_CODE_STRIPPING='NO')
    settings['HEADER_SEARCH_PATHS'] = [quoted(s.replace('$(SRCROOT)', str(ROOT))) for s in settings['HEADER_SEARCH_PATHS']] + [quoted(str(ROOT / 'App')), quoted(str(ROOT / 'NativeHost'))]
    settings['LIBRARY_SEARCH_PATHS'] = [durable(s) for s in settings['LIBRARY_SEARCH_PATHS']]
    settings['OTHER_LDFLAGS'] += [f'-Wl,-force_load,$(BUILT_PRODUCTS_DIR)/lib{name}.a' for name in groups]
    target['dependencies'] += [{'target': name} for name in groups]
    target['dependencies'] += [{'sdk': name + '.framework'} for name in dict.fromkeys(frameworks + ['UIKit', 'AVFAudio']) if name != 'Metal']
    target['info']['path'] = str(OUT / 'Info.plist')
    target['info']['properties'].update(CFBundleDisplayName='Satchel Dev', DMCommit=PIN[:12], DMChain=CHAIN,
        DMShowFPS=True, LSSupportsGameMode=True, GCSupportsControllerUserInteraction=True,
        GCSupportedGameControllers=[{'ProfileName':'ExtendedGamepad'}], CADisableMinimumFrameDurationOnPhone=True)
    base['name'] = 'SatchelDev'
    base['options']['deploymentTarget']['iOS'] = '26.0'
    base['targets'] = {'SatchelDev': target}
    base['schemes'] = {'SatchelDev': {'build': {'targets': {'SatchelDev': 'all'}}, 'run': {'config': 'Release'}}}
    for name, files in groups.items():
        base['targets'][name] = {'type': 'library.static', 'platform': 'iOS', 'sources': files,
            'settings': {'base': {'PRODUCT_NAME': name, 'HEADER_SEARCH_PATHS': [quoted(i) for i in dict.fromkeys(incs)],
                'OTHER_CFLAGS': ['$(inherited)', '-mcpu=apple-a18'] + list(dict.fromkeys(defs)),
                'CLANG_ENABLE_OBJC_ARC': 'YES', 'GCC_C_LANGUAGE_STANDARD': 'gnu11',
                'GCC_OPTIMIZATION_LEVEL': '1', 'DEBUG_INFORMATION_FORMAT': 'dwarf',
                'IPHONEOS_DEPLOYMENT_TARGET': '26.0'}}}
    resources = OUT / 'NativeAssets'; resources.mkdir(exist_ok=True)
    font = resources / 'font'; font.mkdir(exist_ok=True)
    # The standard font is the pinned Noto Sans file from the dependency lock (the bootstrap
    # stages it under the build inputs). Nothing is fetched here.
    noto = '478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823'
    for name in ['standard.ttf', 'nintendo_ext.ttf']:
        cached = ZELDA / 'fonts' / name
        if os.environ.get('SATCHEL_NATIVE_BUILD_INPUTS') and not cached.is_file():
            raise RuntimeError('Missing bootstrap font: ' + name)
        if cached.is_file() and not (font / name).exists():
            shutil.copyfile(cached, font / name)
    if not (font / 'standard.ttf').is_file():
        raise RuntimeError('Missing standard font: stage the pinned Noto Sans file as ' + str(ZELDA / 'fonts/standard.ttf') + ' (scripts/bootstrap_deps.py does this)')
    assert hashlib.sha256((font / 'standard.ttf').read_bytes()).hexdigest() == noto, 'Standard font hash mismatch'
    (font / 'standard_small.ttf').unlink(missing_ok=True)
    if not (font / 'nintendo_ext.ttf').exists():
        run([TOOLS / 'venv/bin/python3', ENGINE / 'dm/tools/make_ext_font.py', font / 'nintendo_ext.ttf'], env=env)
    notices = resources / 'Notices'; notices.mkdir(exist_ok=True)
    for name, relative in {'nxnative': 'LICENSE', 'libvpx': 'dm/third_party/libvpx/LICENSE',
                           'libvpx-patents': 'dm/third_party/libvpx/PATENTS',
                           'stb_vorbis': 'dm/third_party/stb_vorbis/LICENSE', 'bcdec': 'dm/third_party/bcdec/LICENSE',
                           'dav1d': 'dm/third_party/dav1d/COPYING', 'vulkan-headers': 'dm/third_party/vulkan-headers/LICENSE.md',
                           'macnativegames': 'dm/third_party/macnativegames/LICENSE'}.items():
        shutil.copyfile(ENGINE / relative, notices / (name + '.txt'))
    shutil.copyfile(ROOT / 'NativeHost/THIRD_PARTY.md', notices / 'SatchelNativeHost.txt')
    shutil.copyfile(ROOT / 'NativeHost/OFL.txt', notices / 'SIL-OFL.txt')
    shutil.copyfile(ROOT / 'NativeHost/services/RYUJINX-LICENSE.txt', notices / 'Ryujinx-services.txt')
    header = (ENGINE / 'dm/gfx/nvn/include/nvn.h').read_text()
    (notices / 'nvn-declarations.txt').write_text(header[:header.index('*/') + 2] + '\n')
    texture = (ENGINE / 'dm/gfx/nvn/nvn_tex.c').read_text()
    (notices / 'deko3d-attribution.txt').write_text(texture[:texture.index('#include')])
    run([TOOLS / 'venv/bin/python3', '-c',
         'from fontTools.ttLib import TTFont; import sys; f=TTFont(sys.argv[1]); '
         'open(sys.argv[2], "w").write("\\n\\n".join(dict.fromkeys(n.toUnicode() for n in f["name"].names if n.nameID in (0,13,14))))',
         font / 'standard.ttf', notices / 'NotoSans-OFL.txt'], env=env)
    (OUT / 'project.json').write_text(json.dumps(base, indent=2)+'\n')
    run(['xcodegen', 'generate', '--spec', OUT / 'project.json', '--project', OUT])
    return env

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--prepare-only', action='store_true'); parser.add_argument('--archives-only', action='store_true'); parser.add_argument('--unsigned', action='store_true'); args = parser.parse_args()
    if not args.prepare_only and not args.archives_only:
        run(['python3', '-B', ROOT / 'NativeApp/build.py'] + (['--unsigned'] if args.unsigned else []))
        return
    OUT.mkdir(exist_ok=True, parents=True)
    env = prepare(unsigned=args.unsigned)
    if args.prepare_only: return
    heavy_guard()
    # Generic host utility shaders only. No game shader compiler is started here.
    run(['nice', '-n', '10', 'make', '-j2', '-s', '-C', ENGINE / 'dm/host', 'host_msl', 'MSL_SDK=iphoneos'], env=env)
    if args.archives_only:
        for name in ['SatchelNativeHost', 'SatchelNativeGraphics', 'SatchelNativeMedia']:
            run(['nice', '-n', '10', 'xcodebuild', '-quiet', '-project', OUT / 'SatchelDev.xcodeproj',
                 '-target', name, '-configuration', 'Release', '-sdk', 'iphoneos',
                 'SYMROOT=' + str(OUT / 'DerivedData/Build/Products'),
                 'OBJROOT=' + str(OUT / 'DerivedData/Build/Intermediates.noindex'),
                 '-jobs', '2', 'build'], env=env)
        return
    run(['nice', '-n', '10', 'xcodebuild', '-quiet', '-project', OUT / 'SatchelDev.xcodeproj', '-scheme', 'SatchelDev',
         '-configuration', 'Release', '-destination', 'generic/platform=iOS', '-derivedDataPath', OUT / 'DerivedData',
         '-jobs', '2', '-allowProvisioningUpdates', 'build'])
    app = OUT / 'DerivedData/Build/Products/Release-iphoneos/SatchelDev.app'
    run(['codesign', '--verify', '--deep', '--strict', app])
    print('Built and signature verified. No device actions. Ready to install app:', app)

if __name__ == '__main__': main()
