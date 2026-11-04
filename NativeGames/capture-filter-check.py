#!/usr/bin/env python3
"""Check capture privacy using synthetic crash records only."""
from run_capture import filtered_line

rejected = [
    '[dm] *x0: 0x123 0x456 main+0x78\n',
    '[dm] *x28: main+0x123 0x456\n',
    '[dm] fp=0x123  0x100 bytes below sp:\n',
    '[dm]   0 123456789abcdef0 1 abc\n',
    '[dm]   ABCDEF 0 123456789ABCDEF0 42\n',
    'x0=0x123 x1=0x456\n',
    '[dm] OutputDebugString: synthetic payload\n',
    '[dm] instruction bytes 12345678\n',
    '[dm] 12345678 12345678 12345678 12345678 \n',
    '[guest] synthetic payload\n',
]
for line in rejected:assert filtered_line(line) is None
kept = [
    '[dm] [nvn-mtl] FATAL Mac shader compiler failed status=1\n',
    '[dm] bt: main+0x123 sdk+0x456\n',
    '[dm] thread 0x1001 backtrace: main+0x123\n',
    '[dm] [hostpause] ReadFile/0 cumulative calls 4 wall 1.0 ms\n',
]
for line in kept:assert filtered_line(line) == line
assert filtered_line('[dm] layout bytes abcdef\n') == '[dm] layout bytes [omitted]\n'
assert filtered_line('[dm] diagnostic | x19=synthetic\n') == '[dm] diagnostic\n'
print('Capture privacy passed: 10 payload records rejected, symbolic diagnostics retained, inline payloads redacted')
