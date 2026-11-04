#!/usr/bin/env python3
"""Synthetic paired-analysis regression with a known effect and missing counters."""
from pathlib import Path
import copy
import importlib.util

spec = importlib.util.spec_from_file_location('paired', Path(__file__).with_name('gpu-paired-summary.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
shape = '16x16 1c RGBA8Unorm'
work = dict(kind='R', sourceLine=1, shaderWriteStages=0, shape=shape)
samples = [dict(frame=1200+i*120, validEncoders=1, declaredEncoders=1, invalidWorkloads=[],
                spanMs=2+i*.1, encoderSumMs=2+i*.1-(.5 if i%2 else 0),
                workloads=[dict(work, encoderMs=2+i*.1-(.5 if i%2 else 0))]) for i in range(10)]
summary = dict(samples=samples, passWorkloads=[work], sourceSHA256='synthetic')
observations = {s['frame']: (i*4, 10+i*.1) for i, s in enumerate(samples)}
r = m.paired(summary, observations, 1200)
assert r['pairCount'] == 5
assert abs(r['encoderDifferenceMs']['median'] + .4) < 1e-12
assert r['encoderDifferenceMs']['iqr'] < 1e-12
assert abs(r['loadDifference']['median']-.1) < 1e-12
individual = copy.deepcopy(summary)
for sample in individual['samples']:
    sample['passInstances'] = [dict(work, occurrence=1, encoderMs=sample['encoderSumMs']),
                               dict(work, occurrence=2, encoderMs=100)]
r = m.paired(individual, observations, 1200, shape=shape, occurrence=1)
assert abs(r['encoderDifferenceMs']['median']+.4) < 1e-12
assert max(p['a']['encoderSumMs'] for p in r['pairs']) < 3
try: m.paired(individual, observations, 1200, shape=shape, occurrence=1, site=999)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 0'
else: raise AssertionError('A nonexistent baseline target was accepted')
missing_candidate = copy.deepcopy(individual)
missing_candidate['samples'][1]['passInstances'] = [
    dict(work, occurrence=2, encoderMs=100)]
try: m.paired(missing_candidate, observations, 1200, shape=shape, occurrence=1)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
else: raise AssertionError('A missing candidate target was counted as zero time')
for sample in individual['samples']:
    sample['passInstances'].append(dict(work, sourceLine=99, occurrence=1, encoderMs=50))
r = m.paired(individual, observations, 1200, shape=shape, occurrence=1, site=1)
assert max(p['a']['encoderSumMs'] for p in r['pairs']) < 3
phases = copy.deepcopy(summary)
phases['samples'] = []
phase_observations = {}
for i, sample in enumerate(samples):
    for offset in (0, 30, 60, 90):
        row = dict(sample, frame=1200+i*120+offset)
        phases['samples'].append(row)
        phase_observations[row['frame']] = (i*4+offset/30, 10)
assert m.paired(phases, phase_observations, 1200, phase=True)['pairCount'] == 5
missing_phase_sample = copy.deepcopy(phases)
missing_phase_sample['samples'][5]['workloads'] = []
try: m.paired(missing_phase_sample, phase_observations, 1200, phase=True, shape=shape)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
else: raise AssertionError('A missing candidate sample entered a phase median')
phases['samples'].pop()
try: m.paired(phases, phase_observations, 1200, phase=True)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
else: raise AssertionError('An incomplete final phase was accepted')
bad = copy.deepcopy(summary)
bad['samples'][0]['declaredEncoders'] = 2
for kwargs in ({}, {'shape': shape}):
    try: m.paired(bad, observations, 1200, **kwargs)
    except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
    else: raise AssertionError('Unknown missing encoder was accepted')
bad['samples'][0]['invalidWorkloads'] = [dict(shape=shape)]
try: m.paired(bad, observations, 1200, shape=shape)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
else: raise AssertionError('Incomplete target was accepted')
bad['samples'][0]['invalidWorkloads'] = [dict(shape='unrelated')]
r = m.paired(bad, observations, 1200, shape=shape)
assert r['pairCount'] == 5 and r['spanDifferenceMs'] is None
assert r['pairs'][0]['a']['missingEncoders'] == [1]
try: m.paired(bad, observations, 1200)
except ValueError as error: assert str(error) == 'Need 5 complete pairs; found 4'
else: raise AssertionError('Incomplete whole-frame sum was accepted')
print('PASS: five adjacent pairs, known effect, spread, trial load, incomplete target rejection and complete target isolation')
