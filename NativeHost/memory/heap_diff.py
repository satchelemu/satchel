#!/usr/bin/env python3
"""Diff two `heap --sortBySize --showSizes` snapshots by class and size."""
import re
import sys


def load(path):
    rows = {}
    total = None
    for line in open(path, errors='replace'):
        match = re.match(r'All zones: (\d+) nodes \((\d+) bytes\)', line)
        if match: total = (int(match[1]), int(match[2]))
        match = re.match(r'\s*(\d+)\s+(\d+)\s+([\d.]+)\s+(.*?)\s*$', line)
        if match:
            count, size, average, rest = int(match[1]), int(match[2]), float(match[3]), match[4]
            key = (average, re.sub(r'\s{2,}', ' | ', rest))
            old = rows.get(key, (0, 0))
            rows[key] = (old[0] + count, old[1] + size)
    return rows, total


a, ta = load(sys.argv[1])
b, tb = load(sys.argv[2])
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 40
print('totals', ta, '->', tb, 'delta nodes', tb[0] - ta[0], 'delta bytes', tb[1] - ta[1])
deltas = []
for key in set(a) | set(b):
    ca, ba = a.get(key, (0, 0)); cb, bb = b.get(key, (0, 0))
    if ba != bb or ca != cb: deltas.append((bb - ba, cb - ca, key))
deltas.sort(key=lambda row: -abs(row[0]))
print('%12s %10s %10s  %s' % ('delta bytes', 'delta n', 'avg', 'class'))
for dbytes, dcount, key in deltas[:limit]:
    print('%12d %10d %10.1f  %s' % (dbytes, dcount, key[0], key[1]))
