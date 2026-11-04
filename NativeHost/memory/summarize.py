#!/usr/bin/env python3
"""Summarize paired route measurements without reading game payloads."""
from pathlib import Path
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--label', default='route')
args = parser.parse_args()
lines = ['Matched route measurements', '',
         'Counts are sampled near each capture checkpoint. Scene labels require visual review.', '',
         '| Game | Scene | Checkpoint | Variant | Sample frame | Footprint MiB | Metal MiB | Textures | Buffers | Render pipelines | Compute pipelines | Base pipelines | Shaders | Retired shaders | Staging MiB | Ghosts |',
         '|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
pairs = []
for game in ['Castlevania', 'Odyssey', 'TOTK']:
    results = {}
    for variant in ['baseline', 'candidate']:
        work = args.output / 'runs' / game / variant / args.label
        route = json.loads((work / 'route.json').read_text())
        result = json.loads((work / 'result.json').read_text())
        if not result['completed']:
            raise SystemExit('Incomplete route: ' + str(work))
        if result.get('sceneValidated') is not True:
            raise SystemExit('Visual scene review is missing or rejected: ' + str(work))
        results[variant] = (route, result)
    before, after = (results[variant][0] for variant in ['baseline', 'candidate'])
    if before.get('shaderMode', 'pack') != after.get('shaderMode', 'pack'):
        raise SystemExit('Shader mode mismatch: ' + game)
    for key in ['game', 'frames', 'keys', 'checkpoints', 'runtime']:
        if before[key] != after[key]: raise SystemExit('Route mismatch: ' + game + ' ' + key)
    for baseline, candidate in zip(results['baseline'][1]['checkpoints'], results['candidate'][1]['checkpoints'], strict=True):
        if baseline['checkpoint'] != candidate['checkpoint']: raise SystemExit('Checkpoint mismatch: ' + game)
        checkpoint = str(baseline['checkpoint'])
        scene = results['baseline'][1].get('sceneLabels', {}).get(checkpoint)
        if not scene or scene != results['candidate'][1].get('sceneLabels', {}).get(checkpoint):
            raise SystemExit('Reviewed scene labels are missing or different: ' + game + ' ' + checkpoint)
        for variant, row in [('baseline', baseline), ('candidate', candidate)]:
            if abs(row['frame'] - row['checkpoint']) > 60: raise SystemExit('Missing nearby memory sample: ' + game)
            values = [game, scene, str(row['checkpoint']), variant, str(row['frame']),
                      '%.1f' % (row['footprint_bytes'] / 2**20), '%.1f' % (row['metal_bytes'] / 2**20)]
            values += [str(row[key]) for key in ['textures', 'buffers', 'render_pipelines', 'compute_pipelines', 'base_pipelines', 'shaders']]
            values.append(str(row.get('retired_shaders', 0)))
            values += ['%.1f' % (row['arena_bytes'] / 2**20), str(row['ghosts'])]
            lines.append('| ' + ' | '.join(values) + ' |')
        pairs.append(dict(game=game, scene=scene, checkpoint=baseline['checkpoint'], baseline=baseline, candidate=candidate,
                          footprintDeltaMiB=(candidate['footprint_bytes'] - baseline['footprint_bytes']) / 2**20,
                          metalDeltaMiB=(candidate['metal_bytes'] - baseline['metal_bytes']) / 2**20))
(args.output / 'measurements.json').write_text(json.dumps(pairs, indent=2) + '\n')
(args.output / 'measurements.md').write_text('\n'.join(lines) + '\n')
print('PASS: paired routes, complete captures and nearby memory samples for all three games.')
