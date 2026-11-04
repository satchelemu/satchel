#!/usr/bin/env python3
"""Compare real two-lane specialization ownership with an unchanged leak scan."""
import json
import argparse
import os
from pathlib import Path
import re
import subprocess
import runpy

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce the leak from this git revision')
args = parser.parse_args()
OUT = Path(os.environ.get('SESSION_SPECIALIZATION_OUT', Path.home() / 'SatchelBuild/session/round2-specialization'))
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
versions = {}
if args.baseline_ref:
    versions['before'] = subprocess.check_output(['git', 'show', args.baseline_ref + ':NativeHost/graphics/ShaderBuildQueue.inc'], text=True)
versions['after'] = (HERE.parent / 'graphics/ShaderBuildQueue.inc').read_text()
results = []
for name, source in versions.items():
    folder = OUT / name
    folder.mkdir(exist_ok=True)
    (folder / 'ShaderBuildQueue.inc').write_text(source)
    binary = folder / 'specialization-probe'
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-fobjc-arc',
                    '-I' + str(folder), '-framework', 'Foundation', '-framework', 'Metal',
                    str(HERE / 'specialization_probe.m'), '-o', str(binary)], check=True)
    process = subprocess.Popen([str(binary), '100'], env=dict(os.environ, MallocStackLogging='lite'),
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        owners = process.stdout.readline().strip()
        assert re.fullmatch(r'synthetic cycles=100 base_alive=0 specialized_alive=0 compiler_alive=0 library_alive=0', owners), owners
        scan = subprocess.run(['leaks', '--noContent', '--nostacks', '--groupByType', '--quiet', str(process.pid)],
                              capture_output=True, text=True, timeout=30)
        match = re.search(r'(\d+) leaks for (\d+) total leaked bytes', scan.stdout)
        assert match, 'Allocation scan did not return a summary'
        record = dict(version=name, owners=owners, scanExit=scan.returncode,
                      leaks=int(match[1]), leakedBytes=int(match[2]))
        results.append(record)
        (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(record), flush=True)
        if name == 'before':
            assert scan.returncode == 1 and int(match[1]) > 0, 'Concurrent baseline did not reproduce the leak'
        else:
            assert scan.returncode == 0 and int(match[1]) == 0, 'Serialized specialization still leaks'
    finally:
        process.terminate()
        process.wait()
print('PASS: serialized production specialization has zero leaks' +
      ('; concurrent baseline leak reproduced.' if args.baseline_ref else '.'))
