#!/usr/bin/env python3
"""The caution Prepare shows when it leaves out a base's newest imported update: the production
PrepareUpdateCaution and GameInfo.newestUpdate, compiled for macOS against synthetic metadata
only. Usage: run.py <out folder>."""
from pathlib import Path
import runpy
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).expanduser().resolve()
assert not out.is_relative_to(root), 'Keep test products outside the repository'
out.mkdir(parents=True, exist_ok=True)


def between(text, start, end=None):
    assert text.count(start) == 1, start
    first = text.index(start)
    return text[first:] if end is None else text[first:text.index(end, first)]


info = between((root / 'App/GameArtwork.swift').read_text(), 'struct GameInfo: Codable, Equatable {', 'enum GameInfoReader {')
choice = between((root / 'NativePrepare/PrepareJob.swift').read_text(), "/// Plain words for a Prepare that leaves out the base's newest imported update")
stub = 'import SwiftUI\nstruct SwitchAddOnInfo: Codable, Equatable { let titleID: UInt64; let applicationID: UInt64; let version: UInt32 }\n'
(out / 'main.swift').write_text(stub + info + '\n' + choice + '\n' + (root / 'Tests/PrepareUpdate/Tests.swift').read_text())
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2', str(out / 'main.swift'), '-o', str(out / 'prepare-update-tests')], check=True)
subprocess.run([str(out / 'prepare-update-tests')], check=True)
