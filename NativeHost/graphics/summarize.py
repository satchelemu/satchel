#!/usr/bin/env python3
"""Summarize timing-only host TSV data without copying guest diagnostics."""
from pathlib import Path
import argparse, csv, json, math, statistics


def summarize(path, first=1, last=None, budget_ms=None):
    rows = {}
    with path.open() as source:
        reader = csv.reader((line for line in source if not line.startswith('#')), delimiter='\t')
        for row in reader:
            if len(row) != 3 or not row[0].isdigit() or not row[1].startswith('@'):
                continue
            frame, name = int(row[0]), row[1][1:]
            value = float(row[2])
            if frame < first or (last is not None and frame > last) or not math.isfinite(value) or value < 0:
                continue
            rows.setdefault(name, []).append((frame, value))
    result = {'firstRequestedFrame': first, 'lastRequestedFrame': last, 'metrics': {}}
    for name, values in sorted(rows.items()):
        data = sorted(value for _, value in values)
        def percentile(p):
            return data[max(0, math.ceil(p * len(data)) - 1)]
        result['metrics'][name] = dict(count=len(data), mean=statistics.mean(data),
            p50=percentile(.5), p95=percentile(.95), p99=percentile(.99), max=data[-1],
            total=sum(data))
    if budget_ms is not None:
        frames = rows.get('frame_ms', [])
        result['frameBudget'] = dict(ms=budget_ms, count=len(frames),
            overBudget=sum(value > budget_ms for _, value in frames),
            overOneAndHalfBudget=sum(value > budget_ms * 1.5 for _, value in frames),
            effectiveFPS=1000 * len(frames) / sum(value for _, value in frames) if frames and sum(value for _, value in frames) > 0 else None)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('tsv', type=Path)
    p.add_argument('--first', type=int, default=1)
    p.add_argument('--last', type=int)
    p.add_argument('--budget-ms', type=float)
    a = p.parse_args()
    if a.budget_ms is not None and (not math.isfinite(a.budget_ms) or a.budget_ms <= 0):
        p.error('budget must be positive and finite')
    print(json.dumps(summarize(a.tsv, a.first, a.last, a.budget_ms), indent=2))
