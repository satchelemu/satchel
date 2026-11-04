#!/usr/bin/env python3
"""Pair presentation GPU feedback, rejecting fallback, errors and incomplete phases."""
import argparse, importlib.util, json, re, statistics
from pathlib import Path
spec = importlib.util.spec_from_file_location('paired', Path(__file__).with_name('gpu-paired-summary.py'))
paired = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paired)

def summarize(text, first, aa=False, exclude=()):
    phases = {}
    for match in re.finditer(r'\[observed ([\d.]+) load1 ([\d.]+)\].*\[upscale-time\] present (\d+) phase (\d+) spatial (\d+) gpu_ms ([\d.]+) error (\d+)', text):
        wall, load, frame, phase, spatial, ms, error = map(float, match.groups())
        frame, phase = int(frame), int(phase)
        if phase in exclude or frame < first or frame % 120 < 30:
            continue
        expected = 0 if aa else phase % 2
        valid = not error and ms > 0 and spatial == expected and phase == frame // 120
        phases.setdefault(phase, []).append(dict(frame=frame, ms=ms, load=load, valid=valid))
    results = []
    for phase in sorted(phases):
        if phase % 2 or phase + 1 not in phases:
            continue
        a, b = phases[phase], phases[phase + 1]
        if any(len(rows) != 3 or len({r['frame'] for r in rows}) != 3 or not all(r['valid'] for r in rows) for rows in [a,b]):
            continue
        med = lambda rows, key: statistics.median(r[key] for r in rows)
        results.append(dict(phase=phase, offMs=med(a,'ms'), candidateMs=med(b,'ms'),
                            differenceMs=med(b,'ms')-med(a,'ms'), loadDifference=med(b,'load')-med(a,'load')))
    if len(results) < 5:
        raise ValueError('Fewer than five complete pairs with the requested scaler actually used')
    return dict(pairCount=len(results), differenceMs=paired.spread([r['differenceMs'] for r in results]),
                offMs=paired.spread([r['offMs'] for r in results]), candidateMs=paired.spread([r['candidateMs'] for r in results]),
                loadDifference=paired.spread([r['loadDifference'] for r in results]), pairs=results)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('log', type=Path); p.add_argument('--first', type=int, required=True); p.add_argument('--aa', action='store_true'); p.add_argument('--exclude-phase', type=int, action='append', default=[]); a=p.parse_args()
    print(json.dumps(summarize(a.log.read_text(),a.first,a.aa,a.exclude_phase),indent=2))
