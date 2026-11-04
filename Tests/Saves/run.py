#!/usr/bin/env python3
"""Compile the same save implementation used by iOS and test synthetic Mac saves."""
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[2]
out = Path(os.environ.get('SAVE_TEST_OUT', Path.home() / 'SatchelBuild/dlc/save-tests'))
out.mkdir(parents=True, exist_ok=True)
binary = out / 'save-tests'
gate = out / 'save-gate.o'
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fblocks', '-Wall', '-Wextra', '-Werror',
                '-c', root / 'Tests/Saves/SaveGate.m', '-o', gate], check=True)
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-O', '-swift-version', '5',
                '-import-objc-header', root / 'Tests/Saves/SaveGate.h', gate,
                root / 'App/SaveArchive.swift', root / 'App/SaveStore.swift',
                root / 'Tests/Saves/main.swift', '-o', binary], check=True)
with tempfile.TemporaryDirectory(prefix='synthetic-', dir=out) as directory:
    subprocess.run([binary, directory], check=True, timeout=120)
    exports = list((Path(directory) / 'exports').glob('*.zip'))
    assert len(exports) == 1
    with zipfile.ZipFile(exports[0]) as archive:
        assert archive.testzip() is None
        assert archive.read('save/slot/value.dat') == b'synthetic original'
        assert 'save/empty/' in archive.namelist()
        assert archive.read('save/zero.dat') == b''
    print('PASS: independent Python ZIP reader accepts the exported archive and every CRC.')
