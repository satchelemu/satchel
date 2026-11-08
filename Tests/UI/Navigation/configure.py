#!/usr/bin/env python3
"""Generate isolated navigation projects. No device installs or launches."""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[3] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[3]


def write_changed(path, content):
    # Avoid recompiling every Swift source when a targeted run only changes tests.
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def quoted(path):
    # Search-path build settings are tokenized by Xcode, unlike XcodeGen file paths.
    return f'"{path}"' if ' ' in path and not path.startswith('"') else path


def quote_build_paths(project):
    # Quote each list entry, not the entire list. Scalar file settings (including
    # SWIFT_OBJC_BRIDGING_HEADER) and source/info paths are escaped by XcodeGen or
    # Xcode itself. Adding literal quotes there makes them part of the filename.
    for owner in [project, *project['targets'].values()]:
        settings = owner.get('settings', {})
        for group in [settings.get('base', {}), *settings.get('configs', {}).values()]:
            for key, value in group.items():
                if key.endswith('_SEARCH_PATHS'):
                    group[key] = [quoted(path) for path in value] if isinstance(value, list) else quoted(value)


def wait_for_builds():
    while any(Path(line.strip()).name == 'xcodebuild'
              for line in subprocess.check_output(['ps', '-Ao', 'comm='], text=True).splitlines()):
        print('Waiting for the other xcodebuild to finish.', flush=True)
        time.sleep(15)


def add_suite(project, app, target, *, dev=False, preview=False):
    conditions = ['$(inherited)'] + (['NAVIGATION_NATIVE'] if dev else []) + (['NAVIGATION_PREVIEW'] if preview else [])
    project['targets'][target] = {
        'type': 'bundle.ui-testing', 'platform': 'iOS',
        'sources': [{'path': str(ROOT / 'Tests/UI/Navigation'), 'includes': ['*.swift']}],
        'dependencies': [{'target': app}],
        'settings': {'base': {
            'PRODUCT_BUNDLE_IDENTIFIER': 'com.dell.satchel.' + target.lower(),
            'GENERATE_INFOPLIST_FILE': True, 'TEST_TARGET_NAME': app,
            'SWIFT_ACTIVE_COMPILATION_CONDITIONS': ' '.join(conditions),
        }},
    }
    scheme = app + 'Navigation'
    project.setdefault('schemes', {})[scheme] = {
        'build': {'targets': {app: 'all', target: ['test']}},
        'test': {'config': 'Release', 'targets': [{'name': target, 'parallelizable': False}]},
    }
    return scheme


def read_yaml(path):
    # Use macOS's bundled Ruby/Psych instead of installing a Python YAML dependency.
    return json.loads(subprocess.check_output([
        'ruby', '-rjson', '-ryaml', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', str(path)
    ], text=True))


def generate(mode, out):
    out.mkdir(parents=True, exist_ok=True)
    if mode == 'phone-main':
        canonical = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/oneapp')) / 'app/project.json'
        if not canonical.is_file():
            raise SystemExit('Build the single app first: python3 -B NativeApp/build.py')
        project = json.loads(canonical.read_text())
        app = 'Satchel'
        assert project['targets'][app]['settings']['base']['PRODUCT_BUNDLE_IDENTIFIER'] == 'com.dell.satchel'
        scheme = add_suite(project, app, 'SatchelNavigationUITests', dev=True)
    else:
        dev = mode != 'sim-minimal'
        project = read_yaml(ROOT / ('Tests/UIPreview/project-game.yml' if dev else 'Tests/UIPreview/project.yml'))
        engine = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
        if dev:
            sys.path.insert(0, str(ROOT / 'NativeHost'))
            from display_overlay import apply
            apply(ROOT, engine, out, {'SatchelNativeHost': [], 'SatchelNativeGraphics': []})
            # The engine gamepad reads a compiled profile header. Write it from the pinned engine's
            # own public profile, as NativeHost/build.py does, instead of expecting a leftover
            # build folder inside the dependency snapshot (a fresh bootstrap root has none).
            header = out / 'gen/0100F2C0115B6000/game_profile.h'
            header.parent.mkdir(parents=True, exist_ok=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
            subprocess.run([sys.executable, '-B', str(engine / 'dm/games/game.py'), 'header', '0100F2C0115B6000', str(header)],
                           env=dict(env, PYTHONDONTWRITEBYTECODE='1'), check=True, stdout=subprocess.DEVNULL)
        if dev:
            # Exercise the exact controller overlay and native menu with synthetic snapshots.
            import runpy
            pad = out / 'overlays/display/dm/ios/Sources/DmGamepad.m'
            controller_overlay = runpy.run_path(str(ROOT / 'NativeHost/controllers/overlay.py'))
            text = controller_overlay['gamepad'](pad.read_text())
            declaration = '#import <GameController/GameController.h>\nNSArray<GCController *> *SatchelPreviewControllers(void);\n'
            pad.write_text(declaration + text.replace('GCController.controllers', 'SatchelPreviewControllers()'))
            menu = out / 'InGameMenu.m'
            text = (ROOT / 'NativeHost/InGameMenu.m').read_text()
            text = text.replace('#import "../App/ControllerProfiles.h"', '#import "' + str(ROOT / 'App/ControllerProfiles.h') + '"')
            write_changed(menu, declaration + text.replace('GCController.controllers', 'SatchelPreviewControllers()'))
            for row in project['targets']['SatchelPreview']['sources']:
                if row['path'] == '__REPO__/NativeHost/InGameMenu.m': row['path'] = str(menu)
        project = json.loads(json.dumps(project).replace('__REPO__', str(ROOT)).replace(
            '__DISPLAY__/engine', str(engine)).replace('__DISPLAY__', str(out)))
        app = 'SatchelPreview'
        settings = project['targets'][app]['settings']['base']
        settings['PRODUCT_BUNDLE_IDENTIFIER'] = 'com.dell.satchel.uitestpreview' + ('.dev' if dev else '')
        project['targets'][app]['sources'].append({'path': str(ROOT / 'docs/install.md'), 'buildPhase': 'resources'})
        scheme = add_suite(project, app, 'SatchelNavigationUITests', dev=dev, preview=True)
    if mode.startswith('sim-') and dev:
        # Only the device verifier is substituted. Keep PreparedGameView, including
        # stop-before-switch, notifications and dismissal, byte-for-byte production code.
        source = ROOT / 'NativeHost/PreparedGame.swift'
        original = source.read_text()
        boundary = 'try PreparedPackage.verify(root: packageURL)'
        assert original.count(boundary) == 1
        staged = out / 'PreparedGame.swift'
        content = original.replace(boundary, 'try PreviewPackage.verify(root: packageURL)')
        if not staged.exists() or staged.read_text() != content:
            staged.write_text(content)
        for row in project['targets'][app]['sources']:
            if row['path'] == str(source): row['path'] = str(staged)
        # Emulate the menu at the same logical size as the native game surface.
        # Keep the production modal presentation and controller discovery unchanged.
        menu_source = ROOT / 'NativeHost/SatchelMenuSession.swift'
        menu = menu_source.read_text()
        assert menu.count('InGameMenuScreen') == 2
        menu = menu.replace('InGameMenuScreen', 'PreviewMenuScreen')
        menu_staged = out / 'SatchelMenuSession.swift'
        write_changed(menu_staged, menu)
        for row in project['targets'][app]['sources']:
            if row['path'] == str(menu_source): row['path'] = str(menu_staged)
        # Preserve the warm-up lifecycle and cancellation code, replacing only the
        # device verifier for the explicitly enabled synthetic package.
        warmup_source = ROOT / 'NativePrepare/WarmupJob.swift'
        warmup_original = warmup_source.read_text()
        boundary = 'try PreparedPackage.verify(root: context.package, cancelled: { context.token.cancelled })'
        assert warmup_original.count(boundary) == 1
        warmup_staged = out / 'WarmupJob.swift'
        write_changed(warmup_staged, warmup_original.replace(boundary,
            'try PreviewPackage.verifyWarmup(root: context.package, cancelled: { context.token.cancelled })'))
        for row in project['targets'][app]['sources']:
            if row['path'] == str(warmup_source): row['path'] = str(warmup_staged)
        # Prepare's completion, cleanup and outcome stay production code. Only the full package
        # check before cleanup is substituted, for a synthetic Prepare with its own flag.
        prepare_source = ROOT / 'NativePrepare/PrepareJob.swift'
        prepare_original = prepare_source.read_text()
        boundary = 'try PreparedPackage.verify(root: destination, forceFull: true, cancelled: { token.cancelled })'
        assert prepare_original.count(boundary) == 1
        prepare_staged = out / 'PrepareJob.swift'
        write_changed(prepare_staged, prepare_original.replace(boundary,
            'try PreviewPackage.verifyPrepared(root: destination, cancelled: { token.cancelled })'))
        for row in project['targets'][app]['sources']:
            if row['path'] == str(prepare_source): row['path'] = str(prepare_staged)
        # WarmupJob.swift reads the warm-up policy and trial types from the shader module,
        # which the preview does not build. Stage only those two pure types, unchanged.
        policy_source = (ROOT / 'NativeShaders/ShaderWarmup.swift').read_text()
        policy_boundary = '\n// A pure policy so the app and Mac checks share the same bounds.\nstruct SatchelWarmupPolicy'
        assert policy_source.count(policy_boundary) == 1
        policy = out / 'WarmupPolicy.swift'
        write_changed(policy, 'import Foundation\n' + policy_source[policy_source.index(policy_boundary):])
        project['targets'][app]['sources'].append({'path': str(policy)})
        # Hold one real copy after its first chunk, without large files or timing
        # assumptions. The production cancellation and deferred cleanup stay intact.
        import_source = ROOT / 'App/SwitchPackageImport.swift'
        import_original = import_source.read_text()
        boundary = '            progress(copied, size)'
        assert import_original.count(boundary) == 1
        import_staged = out / 'SwitchPackageImport.swift'
        write_changed(import_staged, import_original.replace(boundary, boundary +
            '\n            try ImportCancellationFixture.afterChunk(cancelled: cancelled)'))
        for row in project['targets'][app]['sources']:
            if row['path'] == str(ROOT / 'App'):
                row.setdefault('excludes', []).append('SwitchPackageImport.swift')
        project['targets'][app]['sources'].append({'path': str(import_staged)})
        progress_source = (ROOT / 'NativePrepare/WarmupRunner.swift').read_text()
        progress_boundary = '\nenum WarmupRunner {'
        assert progress_source.count(progress_boundary) == 1
        progress = out / 'WarmupProgress.swift'
        write_changed(progress, progress_source.split(progress_boundary)[0])
        project['targets'][app]['sources'].append({'path': str(progress)})
        # Substitute only document selection when the explicit synthetic-save flag
        # is set. Production save actions, lifecycle, validation and IO stay intact.
        save_source = ROOT / 'App/GameSavePanel.swift'
        save_original = save_source.read_text()
        boundary = 'SaveFileSheet(export: request.export)'
        assert save_original.count(boundary) == 1
        save_content = save_original.replace(boundary, 'PreviewSaveDocumentPicker(export: request.export)')
        save_content = save_content.replace('private struct SaveFileSheet:', 'struct SaveFileSheet:')
        save_staged = out / 'GameSavePanel.swift'
        if not save_staged.exists() or save_staged.read_text() != save_content:
            save_staged.write_text(save_content)
        for row in project['targets'][app]['sources']:
            if row['path'] == str(ROOT / 'App'):
                row.setdefault('excludes', []).append('GameSavePanel.swift')
        project['targets'][app]['sources'].append({'path': str(save_staged)})
        # Substitute only the folder picker when the synthetic cloud-folder flag is set.
        # Bookmark storage, resolution, state and the off switch stay production code.
        cloud_source = ROOT / 'App/CloudSaveSyncPanel.swift'
        cloud_original = cloud_source.read_text()
        boundary = '        picking = true\n'
        assert cloud_original.count(boundary) == 1
        cloud_staged = out / 'CloudSaveSyncPanel.swift'
        write_changed(cloud_staged, cloud_original.replace(boundary,
            '        if let url = CloudSaveFixture.folder() { choose(url); return }\n' + boundary))
        for row in project['targets'][app]['sources']:
            if row['path'] == str(ROOT / 'App'):
                row.setdefault('excludes', []).append('CloudSaveSyncPanel.swift')
        project['targets'][app]['sources'].append({'path': str(cloud_staged)})
        project['targets'][app]['sources'].append({'path': str(ROOT / 'Tests/UIPreview/CloudSaveFixture.swift')})
    if mode.startswith('sim-'):
        peer = 'NavigationBackground'
        project['targets'][peer] = {
            'type': 'application', 'platform': 'iOS',
            'sources': [{'path': str(ROOT / source)} for source in [
                'Tests/UIPreview/NavigationBackground.swift', 'App/GameOrientation.swift']],
            'settings': {'base': {
                'PRODUCT_BUNDLE_IDENTIFIER': 'com.dell.satchel.navigationbackground',
                'TARGETED_DEVICE_FAMILY': '1', 'GENERATE_INFOPLIST_FILE': True,
                'INFOPLIST_KEY_UILaunchScreen_Generation': True,
                'INFOPLIST_KEY_UISupportedInterfaceOrientations':
                    'UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight',
                'INFOPLIST_KEY_UIRequiresFullScreen': True,
            }},
        }
        project['schemes'][scheme]['build']['targets'][peer] = ['test']
    quote_build_paths(project)
    spec = out / 'project.json'
    spec.write_text(json.dumps(project, indent=2) + '\n')
    subprocess.run(['xcodegen', 'generate', '--spec', str(spec), '--project', str(out)], check=True)
    print(f'Project: {out / (project["name"] + ".xcodeproj")}\nScheme: {scheme}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['phone-main', 'sim-main', 'sim-dev', 'sim-minimal'])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    generate(args.mode, args.out.expanduser().resolve())
