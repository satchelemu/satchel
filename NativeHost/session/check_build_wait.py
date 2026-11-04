#!/usr/bin/env python3
"""Keep compiler admission conservative while allowing already-built UI tests."""
from pathlib import Path
import runpy
import subprocess
from unittest.mock import patch

check = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'NativeApp/build_support.py'))['xcode_is_building']
cases = [
    ('xcodebuild -scheme UI test-without-building', False),
    ('xcodebuild -scheme UI -destination "platform=iOS Simulator,name=Phone" test-without-building', False),
    ('xcodebuild -scheme UI test-without-building build', True),
    ('xcodebuild -scheme UI test-without-building build-for-testing', True),
    ('xcodebuild -scheme test-without-building', True),
    ('xcodebuild -scheme build test-without-building', True),
    ('xcodebuild -scheme UI', True),
    ('xcodebuild -scheme UI test', True),
    ('xcodebuild "unterminated', True),
]
for command, expected in cases:
    with patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, command)):
        assert check(123) is expected, command
with patch('subprocess.run', return_value=subprocess.CompletedProcess([], 1, '')):
    assert not check(123)
print('PASS: 10 build admission cases, including ambiguous option values')
