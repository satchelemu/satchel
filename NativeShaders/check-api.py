#!/usr/bin/env python3
"""Check the public API against real records and malformed/cancelled requests."""
import base64
import collections
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):
        raise RuntimeError('Run through guard.py')
    spec = importlib.util.spec_from_file_location('pipeline_check', HERE/'check-pipeline.py')
    pipeline = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pipeline)
    scope, rows = pipeline.jobs()
    pairs = collections.defaultdict(set)
    for table in [pipeline.PROFILE/'shader_pairs.tsv', pipeline.PACKAGES.parent/'pso_pairs.tsv']:
        for line in table.read_text().splitlines():
            if line.strip() and not line.startswith('#'):
                v, p = line.split(); pairs[v].add(p)
    def selected(table):
        path = pipeline.PROFILE/table
        return {line.split()[0] for line in path.read_text().splitlines() if line.strip() and not line.startswith('#')} if path.exists() else set()
    mapping, dual = selected('bf_want.tsv'), selected('dual_want.tsv')
    chosen = []
    for stage in ['vs', 'ps', 'cs']:
        chosen += [key for key in sorted(rows) if rows[key][0].startswith(stage+'_')][:8]
    packs = {}; counts = collections.Counter()
    child = subprocess.Popen([pipeline.ROOT/'api-macosx/api-harness'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        with tempfile.TemporaryDirectory(prefix='api-check-', dir=pipeline.ROOT) as temp:
            scratch = Path(temp); record = scratch/'record.bin'; output = scratch/'result.json'
            def call(request):
                child.stdin.write(json.dumps(request)+'\n'); child.stdin.flush()
                line = child.stdout.readline()
                if not line: raise RuntimeError('API harness exited before response')
                return json.loads(line)
            for key in chosen:
                name, index = rows[key]
                if name not in packs: packs[name] = scope['records']((pipeline.PACKAGES/name).read_bytes())
                record.write_bytes(packs[name][index])
                wanted = set()
                for p in pairs[key]:
                    wanted.update(map(int, re.findall(r'\[\[user\(locn(\d+)\)', (pipeline.CORPUS/(p+'_b.metal')).read_text())))
                request = dict(input=str(record), output=str(output), key=key, stage=name[:2], wanted=sorted(wanted), mappingOnly=key in mapping, dual=key in dual)
                if key in scope['FACTS']: request['facts'] = scope['FACTS'][key]
                assert call(request) == {'status':'ok'}, 'Public API rejected a valid record'
                result = json.loads(output.read_text())
                expected_paths = list(pipeline.CORPUS.glob(key+'_*.metal'))
                expected_sources = {p.stem.split('_')[-1]:p.read_text() for p in expected_paths}
                assert result['sources'] == expected_sources, 'Public API source mismatch'
                reflection = json.loads(base64.b64decode(result['reflectionJSON']))
                expected = json.loads((pipeline.CORPUS/(key+'_b.json')).read_text())
                for field in ['chain_metal','chain_compiler']: expected.pop(field, None)
                expected.update(source='native', pc_paired='')
                assert reflection == expected, 'Public API semantic reflection mismatch'
                counts[name[:2]] += 1; counts['sources'] += len(expected_sources)
                assert call(dict(request,cancelled=True)) == {'status':'error','reason':'cancelled'}
                assert call(dict(request,key='0'*16))['status'] == 'error'
                assert call(dict(request,facts={'key':'0'*16})) == {'status':'error','reason':'invalidRequest'}
                assert call(dict(request,stage='hs')) == {'status':'error','reason':'unsupportedStage'}
                counts['rejections'] += 4
                if name[:2] in ('vs','ps'):
                    assert call(dict(request,stage='gs')) == {'status':'error','reason':'invalidRequest'}
                    counts['rejections'] += 1
            record.write_bytes(b'BTCDBTCD')
            assert call(dict(request,input=str(record)))['status'] == 'error'
            counts['rejections'] += 1
    finally:
        child.stdin.close()
        try: child.wait(timeout=10)
        except subprocess.TimeoutExpired: child.kill(); child.wait()
    assert child.returncode == 0
    result = dict(counts=counts, differences=0, provenance=['source=native','pc_paired=empty'])
    (pipeline.ROOT/'api-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__ == '__main__': main()
