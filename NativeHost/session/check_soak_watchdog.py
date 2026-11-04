#!/usr/bin/env python3
"""Exercise the unchanged 90-second launch watchdog with a silent synthetic host."""
import importlib.util
import json
from pathlib import Path
import tempfile
import time
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
OUT = Path.home() / 'SatchelBuild/session/round4-watchdog-check'
OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=OUT) as temporary:
    root = Path(temporary)
    spec = importlib.util.spec_from_file_location('satchel_soak_watchdog_fixture', HERE / 'soak.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.OUT = root / 'output'
    module.OUT.mkdir()
    module.BUILD = root / 'build'
    module.SAVES = {}
    module.TABLES = {}
    module.CACHES = {}
    for title in module.TITLES:
        module.SAVES[title] = root / 'saves' / title
        module.TABLES[title] = root / 'tables' / title
        module.CACHES[title] = root / 'cache' / title
        for folder in [module.SAVES[title], module.TABLES[title], module.CACHES[title]]:
            folder.mkdir(parents=True)
        (module.SAVES[title] / 'synthetic.txt').write_text('synthetic save\n')
        (module.TABLES[title] / 'shader_map.tsv').touch()
    for relative in ['anygame/host-inputs/Castlevania/main.patched.bin',
                     'anygame/packages/Castlevania/profile.tsv',
                     'anygame/packages/Castlevania/romfs.img']:
        path = module.BUILD / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    host = module.OUT / 'mac-stop-probe'
    host.write_text('#!/bin/sh\nexec /bin/sleep 120\n')
    host.chmod(0o755)
    started = time.monotonic()
    with patch('sys.argv', ['soak.py', '--seconds', '1', '--titles', 'Castlevania',
                            '--label', 'silent-synthetic', '--diagnostic-stop']):
        assert module.main() == 1
    elapsed = time.monotonic() - started
    run = next(module.OUT.glob('silent-synthetic-*'))
    result = json.loads((run / 'result.json').read_text())
    assert result['watchdog'] == 'No numeric host telemetry for 90 seconds'
    assert not result['passed'] and result['completed'] == 0
    assert result['originalSaveUnchanged'] and result['exit'] == -9
    assert elapsed >= 90
    for name in ['watchdog-host-stacks.json', 'watchdog-helper-stacks.json']:
        evidence = (run / name).read_text()
        assert '0x' not in evidence and '/private/' not in evidence
        (OUT / name).write_text(evidence)
    (OUT / 'result.json').write_text(json.dumps(dict(
        elapsed=round(elapsed, 3), runnerExit=1, hostExit=result['exit'],
        watchdog=result['watchdog'], passed=False, originalSaveUnchanged=True,
        fixturePassed=True), indent=2) + '\n')
print('PASS: silent synthetic launch fails after 90 seconds and retains both stack reports')
