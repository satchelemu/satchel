"""Build reusable native sessions without changing the pinned engine."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import runpy

FILES = ['thread.c', 'hle_thread.c', 'hle.c', 'hle_profile.c', 'hle_fs.c', 'hle_os.c', 'crash.c']
MEDIA_FILES = ['hle_movie.c', 'hle_audren.c', 'hle_guestthread.c', 'hle_audio.c']
SERVICE_FILES = runpy.run_path(str(Path(__file__).with_name('services_overlay.py')))['FILES']
OWNER_FILES = ['hle_hid.c', 'hle_touch.c', 'hle_svc.c', 'loader.c', 'main.c']


def bound_crash_report(text):
    anchor = 'static void crash_gate(int sig) {\n'
    assert text.count(anchor) == 1, anchor
    return text.replace(anchor, anchor + '''    // The process alarm can arrive on any thread, including a non-owner.
    // Once a fatal report owns the gate, its timeout must always terminate.
    if (sig == SIGALRM && atomic_load(&g_crash_owner)) _exit(128 + sig);
''')


def prepare(source, destination, overrides=None):
    destination.mkdir(parents=True, exist_ok=True)
    for name in FILES + MEDIA_FILES:
        shutil.copyfile((overrides or {}).get(name, source / name), destination / name)
    crash = destination / 'crash.c'
    crash.write_text(bound_crash_report(crash.read_text()))
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('stop-join.patch')],
                   cwd=destination, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', Path(__file__).with_name('media-stop.patch')],
                   cwd=destination, check=True)
    runpy.run_path(str(Path(__file__).with_name('owners_overlay.py')))['prepare'](source, destination, overrides)
    runpy.run_path(str(Path(__file__).with_name('services_overlay.py')))['prepare'](source, destination, overrides)
    runpy.run_path(str(Path(__file__).with_name('lifecycle_overlay.py')))['host'](destination)
    return destination


def apply(root, engine, out, groups):
    if os.environ.get('SATCHEL_SESSION_BOUNDARY') != '1':
        return
    # Preserve earlier overlays, including crash privacy and menu audio volume.
    overrides = {Path(row['path']).name: Path(row['path'])
                 for row in groups['SatchelNativeHost'] if Path(row['path']).name in FILES + MEDIA_FILES + OWNER_FILES + SERVICE_FILES}
    overlay = prepare(engine / 'dm/host', out / 'overlays/session', overrides)
    session = root / 'NativeHost/session'
    for row in groups['SatchelNativeGraphics']:
        if Path(row['path']).name == 'nvn_mtl.m':
            row['path'] = str(runpy.run_path(str(session / 'graphics_overlay.py'))['prepare'](
                engine, out / 'overlays/session-graphics', Path(row['path'])))
    for row in groups['SatchelNativeGraphics']:
        name = Path(row['path']).name
        script = {'hle_nvn.c': 'nvn_overlay.py', 'nvn_dirty.c': 'dirty_overlay.py'}.get(name)
        if script:
            row['path'] = str(runpy.run_path(str(session / script))['prepare'](Path(row['path']), out / 'overlays/session-graphics' / name))
    for row in groups['SatchelNativeHost']:
        if Path(row['path']).name == 'memstat.m':
            row['path'] = str(runpy.run_path(str(session / 'diagnostics_overlay.py'))['prepare'](
                Path(row['path']), overlay / 'memstat.m'))
        if Path(row['path']).name == 'save_sync.c':
            row['path'] = str(runpy.run_path(str(session / 'save_sync_overlay.py'))['prepare'](
                Path(row['path']), overlay / 'save_sync.c'))
        if Path(row['path']).name == 'front_ios.m':
            row['path'] = str(runpy.run_path(str(session / 'frontend_overlay.py'))['prepare'](
                Path(row['path']), overlay / 'front_ios.m'))
    for rows in groups.values():
        for row in rows:
            path = Path(row['path'])
            if path.name in FILES + MEDIA_FILES + OWNER_FILES + SERVICE_FILES and path == overrides.get(path.name):
                row['path'] = str(overlay / path.name)
            if path.suffix in ('.c', '.m'):
                row['compilerFlags'] = (row.get('compilerFlags', '') +
                    ' -DSATCHEL_MANAGED_SESSION=1 -finstrument-functions -fno-optimize-sibling-calls -I' + shlex.quote(str(session))).strip()
    # The hooks and coordinator must never instrument themselves.
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionBoundary.c')})
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionNativeWorkers.c')})
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionState.c')})
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionInputGate.c')})
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionCoordinator.c')})
    groups['SatchelNativeHost'].append({'path': str(session / 'SessionLifecycle.c')})
