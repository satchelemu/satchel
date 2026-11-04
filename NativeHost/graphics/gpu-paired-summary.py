#!/usr/bin/env python3
"""Compare adjacent Metal counter trials in one interleaved session."""
from pathlib import Path
import argparse
import importlib.util
import json
import re
import statistics


def spread(values):
    ordered = sorted(values)
    quartiles = statistics.quantiles(ordered, n=4, method='inclusive') if len(ordered) > 1 else ordered * 3
    return dict(median=statistics.median(ordered), minimum=min(ordered), maximum=max(ordered),
                q1=quartiles[0], q3=quartiles[2], iqr=quartiles[2] - quartiles[0])


def paired(summary, observations, first, period=120, minimum_pairs=5, phase=False, shape=None, occurrence=None, site=None):
    if (occurrence is not None or site is not None) and not shape:
        raise ValueError('A source site or occurrence requires a shape')
    # Missing counters are never silently treated as zero-cost encoders.
    selected = [s for s in summary['samples'] if s['frame'] >= first]
    trials = {}
    rejected = []
    def selected_work(row):
        rows = [w for w in row['passInstances'] if w['occurrence'] == occurrence] if occurrence else row['workloads']
        return [w for w in rows if w['sourceLine'] == site] if site is not None else rows
    for sample in selected:
        frame = sample['frame']
        index = frame // period if phase else (frame - first) // period
        if phase and frame % period < period // 4:
            continue
        invalid = sample.get('invalidWorkloads', [])
        incomplete = sample['declaredEncoders'] != sample['validEncoders']
        if shape:
            # A complete target can be compared while unrelated encoders lack timestamps.
            # Every missing encoder must be identified, and none may match the target.
            incomplete = (sample['declaredEncoders'] != sample['validEncoders'] + len(invalid)
                          or any(w['shape'] == shape for w in invalid))
            # A disappeared target is not a measured zero-cost pass. This applies
            # to both phases and to every sample used in a phase median.
            incomplete |= not any(w['shape'] == shape for w in selected_work(sample))
        if incomplete or not sample['validEncoders']:
            rejected.append(frame)
            continue
        if frame not in observations:
            raise ValueError('Sample lacks wall-clock and load observation')
        trials.setdefault(index, []).append(sample)
    pairs = []
    for index in sorted(trials):
        if index % 2 or index + 1 not in trials:
            continue
        if phase and (len(trials[index]) < 3 or len(trials[index + 1]) < 3):
            continue
        if shape and not any(w['shape'] == shape for r in trials[index] for w in selected_work(r)):
            continue
        sides = []
        for side in (index, index + 1):
            rows = trials[side]
            loads = [observations[r['frame']][1] for r in rows]
            sides.append(dict(phase=side, frames=[r['frame'] for r in rows], load1=spread(loads),
                              observedStart=min(observations[r['frame']][0] for r in rows),
                              observedEnd=max(observations[r['frame']][0] for r in rows),
                              missingEncoders=[r['declaredEncoders'] - r['validEncoders'] for r in rows],
                              encoderSumMs=statistics.median(sum(w['encoderMs'] for w in selected_work(r) if w['shape'] == shape) if shape else r['encoderSumMs'] for r in rows),
                              spanMs=None if shape else statistics.median(r['spanMs'] for r in rows)))
        pairs.append(dict(a=sides[0], b=sides[1],
                          encoderDifferenceMs=sides[1]['encoderSumMs'] - sides[0]['encoderSumMs'],
                          spanDifferenceMs=None if shape else sides[1]['spanMs'] - sides[0]['spanMs'],
                          loadDifference=sides[1]['load1']['median'] - sides[0]['load1']['median']))
    if len(pairs) < minimum_pairs:
        raise ValueError(f'Need {minimum_pairs} complete pairs; found {len(pairs)}')
    def key(row):
        return row['kind'], row['sourceLine'], row['shaderWriteStages'], row['shape']
    workloads = []
    for workload in summary['passWorkloads']:
        if shape and workload['shape'] != shape:
            continue
        if site is not None and workload['sourceLine'] != site:
            continue
        identity = key(workload)
        aa, bb = [], []
        for pair in pairs:
            for side, result in (('a', aa), ('b', bb)):
                values = [sum(w['encoderMs'] for w in selected_work(r) if key(w) == identity)
                          for r in trials[pair[side]['phase']]]
                result.append(statistics.median(values))
        workloads.append(dict(**{k: workload[k] for k in ('kind', 'sourceLine', 'shaderWriteStages', 'shape')},
                              aMs=spread(aa), bMs=spread(bb),
                              pairedDifferenceMs=spread([b - a for a, b in zip(aa, bb)])))
    workloads.sort(key=lambda w: -w['aMs']['median'])
    return dict(pairCount=len(pairs), pairs=pairs, rejectedFrames=rejected, passWorkloads=workloads, selectedShape=shape, selectedOccurrence=occurrence, selectedSourceLine=site,
                encoderDifferenceMs=spread([p['encoderDifferenceMs'] for p in pairs]),
                spanDifferenceMs=None if shape else spread([p['spanDifferenceMs'] for p in pairs]),
                loadDifference=spread([p['loadDifference'] for p in pairs]),
                sourceSHA256=summary['sourceSHA256'],
                experimentSHA256=summary.get('experimentSHA256', {}),
                interpretation='B minus A, negative is lower. Adjacent trials reduce shared-load drift; '
                'they do not guarantee identical contention. A/A estimates repeatability, not speedup. '
                'Encoder sums may overlap and are not GPU busy time. Phase mode discards the first quarter.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('source', type=Path)
    parser.add_argument('--first', type=int, required=True)
    parser.add_argument('--last', type=int)
    parser.add_argument('--period', type=int, default=120)
    parser.add_argument('--phase', action='store_true', help='Group samples by DM_AB phases')
    parser.add_argument('--shape', help='Compare only a fully timestamped attachment shape')
    parser.add_argument('--occurrence', type=int, help='Compare one occurrence per shape and ending site')
    parser.add_argument('--site', type=int, help='Select the generated source line ending the pass')
    args = parser.parse_args()
    if args.period < 4 or args.first < 0:
        parser.error('Period must be at least 4 and first frame nonnegative')
    if args.occurrence is not None and (args.occurrence < 1 or not args.shape):
        parser.error('Occurrence must be positive and requires a shape')
    if args.site is not None and not args.shape:
        parser.error('A source site requires a shape')
    spec = importlib.util.spec_from_file_location('cost_summary', Path(__file__).with_name('gpu-cost-summary.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    summary = module.summarize(args.log, args.source, args.first, args.last)
    pattern = re.compile(r'\[observed ([\d.]+) load1 ([\d.]+)\].*\[gpuprof\] frame (\d+):')
    observations = {int(m[3]): (float(m[1]), float(m[2])) for m in pattern.finditer(args.log.read_text())}
    print(json.dumps(paired(summary, observations, args.first, args.period, phase=args.phase, shape=args.shape, occurrence=args.occurrence, site=args.site), indent=2))


if __name__ == '__main__':
    main()
