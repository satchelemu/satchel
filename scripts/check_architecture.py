#!/usr/bin/env python3
"""Satchel's host and importer stay general: their code names no game by its title ID.

Adapted from nxnative's dm/tools/tests/test_architecture.py (MIT, upstream commit
c7da003c, see NativeHost/THIRD_PARTY.md). Per-game values belong in data: the reviewed
profile registry (App/NativeGameProfiles.json), the runtime profiles
(App/NativeRuntimeProfiles.json), the player presets (NativeHost/PlayerProfiles.plist)
and the compatibility documents. None of those is code, so none is scanned.

Scanned: tracked C, Objective-C, assembly, include and Swift sources under NativeHost/
(except the generated Ryujinx coverage tables in NativeHost/services/coverage) and App/,
plus the lines that NativeHost's engine patches add. Tests, probes, checks and simulator
fixtures are left out, as upstream leaves out its tests: they may name the title they
exercise. A comment may cite a title ID; code may not.

Run directly (exit status 1 on a violation) or with --self-test for the pattern tests.
No game files needed."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
ROOTS = ['NativeHost', 'App']
CODE_EXT = ('.c', '.m', '.mm', '.h', '.inc', '.S', '.swift')
EXCLUDED_DIRS = ('NativeHost/services/coverage/',)
# A file is test-only when its name says so. Each word is matched against the file stem.
TEST_WORDS = re.compile(r'(^|[_.-])(test|tests|probe|check|checks|fixture|simulator|compare|equivalence)([_.-]|$)|Checks$|^lifecycle_(host_call|guest)$', re.I)

# The upstream pattern: Switch (01) or Switch 2 (04), a base game (000), its update (800)
# or another program (001), with or without 0x and C's optional leading zero.
# 0100000000000xxx is firmware content, not a game.
TITLE_ID = re.compile(r'(?<![0-9A-Za-z_])(?:0[xX]0?|0)(?![14]0{11})[14][0-9A-Fa-f]{11}[08]0[0-9A-Fa-f](?![0-9A-Fa-f])')


def is_test(path):
    return bool(TEST_WORDS.search(path.stem))


def scanned_files():
    tracked = subprocess.run(['git', 'ls-files', *ROOTS], cwd=REPO, capture_output=True, text=True, check=True).stdout.split('\n')
    out = []
    for name in sorted(set(filter(None, tracked))):
        if name.startswith(EXCLUDED_DIRS): continue
        path = REPO / name
        if path.suffix in CODE_EXT + ('.patch',) and not is_test(path) and path.is_file():
            out.append(path)
    return out


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    return [re.sub(r'//.*', '', line) for line in text.splitlines()]


def code_lines(path):
    """(line number, code without comments). For a patch, only the lines it adds."""
    text = path.read_text(errors='replace')
    if path.suffix == '.patch':
        added = [(n, line[1:]) for n, line in enumerate(text.splitlines(), 1)
                 if line.startswith('+') and not line.startswith('+++')]
        stripped = strip_comments('\n'.join(line for _, line in added))
        return [(n, code) for (n, _), code in zip(added, stripped)]
    return list(enumerate(strip_comments(text), 1))


def violations():
    return [f'{p.relative_to(REPO)}:{n}: {line.strip()[:120]}'
            for p in scanned_files() for n, line in code_lines(p) if TITLE_ID.search(line)]


class TitleIds(unittest.TestCase):
    def test_the_pattern(self):
        for s in ('0x0100F2C0115B6000ull', '0x0100F2C0115B6800', '0x100F2C0115B6000', '"0100F2C0115B6000"',
                  '0x0400D4201C6EA000', '0x010049900F546001', 'x == 0100f2c0115b6000', '@"0100000000010000"'):
            self.assertIsNotNone(TITLE_ID.search(s), s)
        for s in ('0x0100000000000802ull', '0x0100000000000000', '"d0f769bb916bbba9"', '0x0100F2C0115B6000F',
                  'k0100F2C0115B6000', '0x100F2C0115B600', 'cd4004af194a51712812c680f0a028ba9152c0f8'):
            self.assertIsNone(TITLE_ID.search(s), s)

    def test_the_scan_reaches_the_host_and_importer(self):
        files = {str(p.relative_to(REPO)) for p in scanned_files()}
        self.assertLessEqual({'NativeHost/SessionBridge.m', 'NativeHost/PlayerSettings.m', 'NativeHost/InGameMenu.m',
                              'NativeHost/session/SessionCoordinator.c', 'NativeHost/display/screen-modes.patch',
                              'App/NativeGameProfile.swift', 'App/SwitchPackageImport.swift'}, files)
        self.assertFalse([f for f in files if f.startswith('NativeHost/services/coverage/')])
        self.assertFalse([f for f in files if Path(f).name.startswith('test_') or '_probe' in f])

    def test_comments_are_left_out(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, 'a.c')
            p.write_text('// 0100F2C0115B6000\n/* 0100F2C0115B6000\n 0100F2C0115B6000 */ int a;\nint b = 0x0100F2C0115B6000ull; // why\n')
            self.assertEqual([n for n, line in code_lines(p) if TITLE_ID.search(line)], [4])

    def test_patches_scan_only_added_lines(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, 'a.patch')
            p.write_text('--- a/x.c\n+++ b/x.c\n@@ -1 +1 @@\n-int a = 0x0100F2C0115B6000;\n+int a = 0; // 0100F2C0115B6000\n+int b = 0x0100F2C0115B6000;\n')
            self.assertEqual([n for n, line in code_lines(p) if TITLE_ID.search(line)], [6])

    def test_test_files_are_recognized(self):
        for name in ('test_ipc.c', 'check.m', 'audio_probe.c', 'simulator.m', 'fixture.S', 'PhoneShaderRuntimeChecks.swift',
                     'remapping_check.m', 'frame-shot-test.inc', 'request-equivalence.swift', 'lifecycle_host_call.c'):
            self.assertTrue(is_test(Path(name)), name)
        for name in ('SessionBridge.m', 'PlayerSettings.m', 'SessionCoordinator.c', 'InGameMenu.m', 'screen-modes.patch',
                     'SwitchPackageImport.swift', 'NativeGameProfile.swift'):
            self.assertFalse(is_test(Path(name)), name)


def main():
    if '--self-test' in sys.argv:
        sys.argv.remove('--self-test')
        unittest.main()
    hits = violations()
    for hit in hits: print(hit)
    if hits:
        print("FAIL: a title ID in host or importer code. Put the game's value in its profile data, not in code.", file=sys.stderr)
        return 1
    print(f'Architecture check passed: {len(scanned_files())} host and importer files name no title ID.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
