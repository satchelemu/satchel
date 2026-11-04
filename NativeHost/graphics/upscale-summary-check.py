#!/usr/bin/env python3
"""Synthetic paired-trial completeness and fallback checks."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('summary',Path(__file__).with_name('upscale-summary.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def rows(aa=False):
    return [f'[observed {f}.0 load1 4.0] [upscale-time] present {f} phase {f//120} spatial {0 if aa else f//120%2} gpu_ms {1 if aa else 1+f//120%2} error 0'
            for f in range(0,1200,30)]
r=m.summarize('\n'.join(rows()),0)
assert r['pairCount']==5 and r['differenceMs']['median']==1
assert m.summarize('\n'.join(rows(True)),0,True)['differenceMs']['median']==0
for changed in [rows()[:-1], [s.replace('spatial 1','spatial 0') for s in rows()],
                [s.replace('error 0','error 1') if 'present 150 ' in s else s for s in rows()],
                [s.replace('gpu_ms 2','gpu_ms 0') for s in rows()], rows()+[rows()[1]]]:
    try:m.summarize('\n'.join(changed),0)
    except ValueError:pass
    else:raise AssertionError('Incomplete or invalid pairs were accepted')
print('PASS: known cost, A/A, missing sample, fallback, GPU error, zero timestamp and duplicate rejection')
