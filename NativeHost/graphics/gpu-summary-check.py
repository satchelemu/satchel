#!/usr/bin/env python3
"""Synthetic checks for overlapping GPU intervals and incomplete counter data."""
from pathlib import Path
import importlib.util
import os
import tempfile

spec = importlib.util.spec_from_file_location('gpu_summary', Path(__file__).with_name('gpu-cost-summary.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
out = Path(os.environ['SATCHEL_OUT'])
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='gpu-summary-', dir=out) as temporary:
    root = Path(temporary)
    source = root / 'source.m'
    source.write_text(''.join(f'source line {i}\n' for i in range(1, 11)))
    log = root / 'timing.log'
    log.write_text('[dm] [nvn-mtl] [gpuprof] F120 0 1000.0 us render pass 1 16x16 colors 1 depth 0 loads 1 draws 1 start +0.0 seg 0 load 1 KB store 1 KB fmt 70/0 end_site 8 writes 1\n[dm] [nvn-mtl] [gpuprof] F120 1 1000.0 us render pass 2 16x16 colors 1 depth 0 loads 1 draws 1 start +500.0 seg 0 load 1 KB store 1 KB fmt 70/0 end_site 9 writes 2\n[dm] [nvn-mtl] [gpuprof] frame 120: render 2.00 ms, compute / copy 0.00 ms over 3 encoders (2 render passes, attachments load 0.0 MB store 0.0 MB), first start to last end 1.50 ms\n')
    result = module.summarize(log, source)
    assert result['spanMedianMs'] == 1.5
    sample = result['samples'][0]
    assert sample['encoderSumMs'] == 2.0 and sample['idleMs'] == 0
    assert sample['validEncoders'] == 2 and sample['declaredEncoders'] == 3
    assert {row['shaderWriteStages'] for row in result['passEndSites']} == {1, 2}
    assert {row['source'] for row in result['passEndSites']} == {'source line 8', 'source line 9'}
    assert len(result['passWorkloads']) == 2
    assert sum(row['encoderMsPerFrame'] for row in result['passWorkloads']) == 2.0
    log.write_text(log.read_text() + '[gpuprof-invalid] F120 2 kind R size 8x8 colors 0 depth 1 fmt 0/252 end_site 10 writes 0 draws 0\n')
    labelled = module.summarize(log, source)['samples'][0]
    assert labelled['validEncoders'] == 2 and labelled['declaredEncoders'] == 3
    assert labelled['invalidWorkloads'] == [dict(index=2, kind='R', shape='8x8 0c+d Depth32Float', sourceLine=10, shaderWriteStages=0, draws=0)]
    assert labelled['encoderSumMs'] == 2.0
    try:
        module.summarize(log, source, first=121)
    except ValueError as error:
        assert str(error) == 'No sampled GPU encoders in the requested interval'
    else:
        raise AssertionError('An empty interval was accepted')
    log.write_text(log.read_text() + '[gpuprof] frame 240: render 0.00 ms, compute / copy 0.00 ms over 1 encoders, first start to last end 0.00 ms\n')
    result = module.summarize(log, source)
    assert result['sampleCount'] == 2
    assert result['samples'][1]['validEncoders'] == 0
    assert result['samples'][1]['declaredEncoders'] == 1
    assert result['samples'][1]['spanMs'] is None
    assert sum(row['encoderMsPerFrame'] for row in result['passWorkloads']) == 1.0
    log.write_text(log.read_text() + '[gpuprof] F360 0 3000.0 us compute / copy after pass 2 start +0.0 seg 0 copies 4 (uniform updates 2) dispatches 1\n')
    result = module.summarize(log, source)
    assert result['passWorkloads'][0]['kind'] == 'C'
    assert result['passWorkloads'][0]['meanEncoderUs'] == 3000
    assert result['passWorkloads'][0]['shape'] == 'compute / copy: 1 dispatches, 4 copies, 2 uniform updates'
print('PASS: overlap, missing timestamp, stage mask, source attribution, render/compute workload ranking, empty and invalid intervals')
