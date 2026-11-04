#!/usr/bin/env python3
"""Exercise shared native preparation with original synthetic compressed fixtures."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
main = out / 'CompatibilityMain.swift'
fixtures = ['Tests/switch_executable.swift', 'Tests/switch_romfs.swift', 'Tests/switch_content.swift', 'Tests/package_reader.swift']
main.write_text('\n'.join((root / f).read_text().replace('@main', '') for f in fixtures) + '\n' +
                (root / 'NativePrepare/CompatibilityTests.swift').read_text())
env = dict(os.environ, PREPARE_OUT=str(out / 'core'), PREPARE_MAIN=str(main),
           PREPARE_EXECUTABLE=str(out / 'compatibility-tests'), TMPDIR=str(out) + '/')
subprocess.run(['zsh', root / 'NativePrepare/build-core.zsh'], env=env, check=True)
sys.path.insert(0, str(root / 'Tools/NativeRewriter/tests'))
from differential import fixture
(out / 'nro-fixture.bin').write_bytes(fixture([0xd65f03c0])[0x100:])
subprocess.run([out / 'compatibility-tests', out / 'synthetic'], env=env, check=True)
