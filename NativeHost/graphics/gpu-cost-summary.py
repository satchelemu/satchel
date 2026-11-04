#!/usr/bin/env python3
"""Attribute sampled Metal encoder time to the source sites that ended passes."""
from pathlib import Path
import argparse
import collections
import hashlib
import importlib.util
import json
import os
import re
import statistics


def summarize(log, source, first=1, last=None):
    engine = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
    spec = importlib.util.spec_from_file_location('gpuprof', engine / 'dm/tools/gpuprof.py')
    profile = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profile)
    lines = log.read_text().splitlines()
    frames, totals = profile.parse(lines)
    selected = {f: frames.get(f, []) for f in sorted(frames.keys() | totals.keys())
                if f >= first and (last is None or f <= last)}
    if not selected:
        raise ValueError('No sampled GPU encoders in the requested interval')
    sites = {}
    pattern = re.compile(r'\[gpuprof\] F(\d+)\s+(\d+).*end_site (\d+)(?: writes (\d+))?')
    for line in lines:
        match = pattern.search(line)
        if match:
            sites[int(match[1]), int(match[2])] = (int(match[3]), int(match[4]) if match[4] else None)
    source_text = source.read_text()
    source_lines = source_text.splitlines()
    invalid = collections.defaultdict(list)
    invalid_pattern = re.compile(r'\[gpuprof-invalid\] F(\d+) (\d+) kind (\w) size (\d+)x(\d+) colors (\d+) depth (\d+) fmt (\d+)/(\d+) end_site (\d+) writes (\d+) draws (\d+)')
    for line in lines:
        match = invalid_pattern.search(line)
        if not match:
            continue
        f, idx = int(match[1]), int(match[2])
        kind = match[3]
        w, h, colors, depth, cfmt, dfmt, site, writes, draws = map(int, match.groups()[3:])
        encoder = profile.Enc(f, idx, 0, kind, 0, w, h, colors, depth, 0, draws, 0, 0, 0, 0, cfmt, dfmt, 0, 0, 0)
        invalid[f].append(dict(index=idx, kind=kind, shape=profile.shape_key(encoder),
                               sourceLine=site, shaderWriteStages=writes, draws=draws))
    groups = collections.defaultdict(lambda: [0, 0.0, 0, 0])
    shapes = collections.defaultdict(lambda: [0, 0.0, 0, 0])
    instances = collections.defaultdict(list)
    samples = []
    for frame, encoders in selected.items():
        frame_workloads = collections.defaultdict(float)
        occurrences = collections.Counter()
        frame_instances = []
        stat = profile.frame_stats(encoders) if encoders else None
        declared = totals.get(frame, {}).get('encoders')
        samples.append(dict(frame=frame, validEncoders=len(encoders), declaredEncoders=declared,
                            invalidWorkloads=invalid[frame],
                            spanMs=stat['span_us'] / 1000 if stat else None,
                            idleMs=stat['gap_us'] / 1000 if stat else None,
                            encoderSumMs=stat['sum_us'] / 1000 if stat else None))
        for encoder in encoders:
            site, writes = sites.get((frame, encoder.idx), (0, None))
            if encoder.kind == 'R':
                group = groups[site, writes]
                group[0] += 1
                group[1] += encoder.us
                group[2] += encoder.load_kb
                group[3] += encoder.store_kb
                label = profile.shape_key(encoder)
            else:
                label = (f'compute / copy: {encoder.dispatches} dispatches, '
                         f'{encoder.copies} copies, {encoder.ubd} uniform updates')
            shape = shapes[encoder.kind, site, writes, label]
            shape[0] += 1
            shape[1] += encoder.us
            shape[2] += encoder.load_kb
            shape[3] += encoder.store_kb
            frame_workloads[encoder.kind, site, writes, label] += encoder.us / 1000
            identity = (encoder.kind, site, writes, label)
            occurrences[identity] += 1
            instance = dict(kind=encoder.kind, sourceLine=site, shaderWriteStages=writes,
                            shape=label, occurrence=occurrences[identity], encoderMs=encoder.us / 1000,
                            encoderIndex=encoder.idx, draws=encoder.draws)
            frame_instances.append(instance)
            instances[(*identity, occurrences[identity])].append(encoder.us / 1000)
        samples[-1]['passInstances'] = frame_instances
        samples[-1]['workloads'] = [dict(kind=k[0], sourceLine=k[1], shaderWriteStages=k[2],
                                       shape=k[3], encoderMs=v)
                                    for k, v in frame_workloads.items()]
    count = len(selected)
    rows = []
    for (site, writes), (passes, us, load, store) in sorted(groups.items(), key=lambda pair: -pair[1][1]):
        rows.append(dict(sourceLine=site, shaderWriteStages=writes,
                         passesPerFrame=passes / count, encoderMsPerFrame=us / count / 1000,
                         loadMiBPerFrame=load / count / 1024, storeMiBPerFrame=store / count / 1024,
                         source=source_lines[site - 1].strip() if 0 < site <= len(source_lines) else None))
    workloads = []
    for (kind, site, writes, shape), (passes, us, load, store) in sorted(shapes.items(), key=lambda pair: -pair[1][1]):
        workloads.append(dict(kind=kind, sourceLine=site, shaderWriteStages=writes, shape=shape,
                              passesPerFrame=passes / count, encoderMsPerFrame=us / count / 1000,
                              meanEncoderUs=us / passes, loadMiBPerFrame=load / count / 1024,
                              storeMiBPerFrame=store / count / 1024))
    spans = [row['spanMs'] for row in samples if row['spanMs'] is not None]
    return dict(sampleCount=count, samples=samples, passEndSites=rows, passWorkloads=workloads,
                passInstances=sorted([dict(kind=k[0], sourceLine=k[1], shaderWriteStages=k[2],
                                           shape=k[3], occurrence=k[4], samples=len(v),
                                           medianEncoderMs=statistics.median(v), minimumEncoderMs=min(v), maximumEncoderMs=max(v))
                                      for k, v in instances.items()], key=lambda row: -row['medianEncoderMs']),
                spanMedianMs=statistics.median(spans) if spans else None,
                sourceSHA256=hashlib.sha256(source_text.encode()).hexdigest(),
                experimentSHA256={name: hashlib.sha256((source.parent / name).read_bytes()).hexdigest()
                                  for name in ('GpuCost.inc', 'ConsumerBarrier.inc', 'AttachmentCost.inc', 'DepthCost.inc')
                                  if '#include "' + name + '"' in source_text and (source.parent / name).exists()},
                interpretation='Encoder attribution is not removable overhead or whole-frame GPU busy time. '
                'Spans include idle gaps. A missing timestamp makes encoder attribution incomplete; '
                'per-frame workload means include all declared frames and are lower bounds when timestamps are missing. '
                'Write-stage bits are vertex 1, fragment 2, other 4; null means uninstrumented.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('source', type=Path, help='Exact generated nvn_mtl.m used to build this host')
    parser.add_argument('--first', type=int, default=1)
    parser.add_argument('--last', type=int)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log, args.source, args.first, args.last), indent=2))
