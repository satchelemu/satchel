"""Apply the display patch to generated overlays, leaving the pinned source copy pristine."""
from pathlib import Path
import shutil
import subprocess


def apply(root, engine, out, groups):
    overlay = out / 'overlays/display'
    paths = ['dm/ios/Sources/front_ios.m', 'dm/ios/Sources/DmGamepad.m', 'dm/host/hle_hid.c', 'dm/host/hle_audio.c', 'dm/gfx/nvn/hle_nvn.c', 'dm/gfx/nvn/nvn_mtl.m']
    for relative in paths:
        destination = overlay / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(engine / relative, destination)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/display/screen-modes.patch'],
                   cwd=overlay, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/display/frame-shot.patch'],
                   cwd=overlay, check=True)
    # The presented picture's shape and the screen-shape state are the engine's own since the
    # f8a5618d pin (nxnative 600c0b37 and 48d79ced; see NativeHost/display/README.md).
    # The general capture adapter records observed programs and draw pairs on iOS.
    # Its payload remains under the session's private DM_NVN_SHDUMP directory.
    shutil.copyfile(root / 'NativeGames/RuntimeCapture.inc', overlay / 'dm/gfx/nvn/RuntimeCapture.inc')
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeGames/runtime-capture.patch'],
                   cwd=overlay, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/phone-capture-lifetime.patch'],
                   cwd=overlay, check=True)
    for name in ['PhoneShaderRuntime.h', 'PhoneShaderRuntime.inc']:
        shutil.copyfile(root / 'NativeHost' / name, overlay / 'dm/gfx/nvn' / name)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/phone-shaders.patch'],
                   cwd=overlay, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/phone-prefetch.patch'],
                   cwd=overlay, check=True)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', root / 'NativeHost/phone-ahead.patch'],
                   cwd=overlay, check=True)
    for row in groups['SatchelNativeHost'] + groups['SatchelNativeGraphics']:
        path = Path(row['path'])
        if path.is_relative_to(engine) and str(path.relative_to(engine)) in paths:
            row['path'] = str(overlay / path.relative_to(engine))
    for name in ['PlayerSettings.m', 'DisplayControls.m', 'InGameMenu.m']:
        groups['SatchelNativeHost'].append({'path': str(root / 'NativeHost' / name)})
