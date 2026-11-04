#!/usr/bin/env python3
"""Run the real Swift verifier and cleanup against synthetic files on macOS."""
import os
from pathlib import Path
import runpy
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).expanduser().resolve()
assert not out.is_relative_to(root), 'Keep test products outside the repository'
out.mkdir(parents=True, exist_ok=True)
# The view file also contains the shared verifier. Compile that exact implementation
# with only the iOS session bridge stubbed and all storage redirected into this test.
source = (root / 'NativeHost/PreparedGame.swift').read_text().split('\nstruct PreparedGameView: View {')[0]
source = source.replace('import SwiftUI', 'import Foundation')
source = source.replace('FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]', 'testRoot.appendingPathComponent("Support")')
source = source.replace('fm.urls(for: .documentDirectory, in: .userDomainMask)[0]', 'testRoot.appendingPathComponent("Documents")')
source = source.replace('FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]', 'testRoot.appendingPathComponent("Documents")')
source += '\nfunc SatchelNativeSessionStarted() -> Bool { false }\n'
cancellation = (root / 'NativePrepare/Rewrite.swift').read_text().split('private final class RewriteProgressContext')[0]
(out / 'Cancellation.swift').write_text(cancellation)
(out / 'main.swift').write_text(source + '\n' + (root / 'Tests/PrepareCleanup/Tests.swift').read_text())
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
command = ['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2',
           root / 'App/NativeGameProfile.swift', root / 'App/PreparedAddOn.swift', root / 'App/SwitchPackage.swift',
           root / 'App/HomebrewItem.swift', root / 'App/PreparedImportCleanup.swift',
           out / 'Cancellation.swift', out / 'main.swift', '-o', out / 'cleanup-tests']
subprocess.run([str(x) for x in command], check=True)
subprocess.run([str(out / 'cleanup-tests'), str(out)], check=True)
