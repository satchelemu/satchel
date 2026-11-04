#!/usr/bin/env python3
"""Synthetic privacy checks for the retained host and helper metadata."""
from pathlib import Path
import subprocess
import tempfile
import json
from unittest.mock import patch
from soak_diagnostics import allocation_metadata, helper_samples, host_sample, red_category, write_process_samples

sample = '''Process: private-process
Path: /private/fixture/guest
  + 4 lib_from_file (in mac-stop-probe) [0x12345678]
  + 3 pthread_mutex_lock (in libsystem_pthread.dylib) [0xabcdef]
  + 2 private_guest_symbol (in guest-image) [0x87654321]
  + 1 ??? (in mac-stop-probe) [0x1234]
'''
with patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, sample)):
    result = host_sample(42)
assert len(result['frames']) == 2
retained = str(result)
assert all(value not in retained for value in ['private_guest', '/private', '0x', '???'])
listing = '42 1 /private/mac-stop-probe\n43 42 /private/Python\n44 43 /private/metal\n45 1 /private/unrelated\n'
with patch('subprocess.check_output', return_value=listing), patch('soak_diagnostics.host_sample', return_value=result):
    helpers = helper_samples(42)
assert [row['pid'] for row in helpers] == [43, 44]
assert [row['executable'] for row in helpers] == ['Python', 'metal']
assert '/private' not in str(helpers)
metadata = allocation_metadata('''2 leaks for 64 total leaked bytes
  2 (64 bytes) ROOT LEAK: object 0x1234
  mac-stop-probe host_symbol 0x5678
  guest-image private_guest_symbol
  Path: /private/fixture/guest
''')
assert '2 leaks for 64 total leaked bytes' in metadata
assert all(value not in metadata for value in ['private_guest', '/private', '0x'])
assert red_category('[RED] missing shader private-value', [('missing shader ', 'missing shader %s')]) == 'missing shader %s'
assert red_category('[RED] private-value', []) == 'Unclassified RED diagnostic'
print('PASS: symbolic host filtering, owned-helper selection, allocation redaction and literal categories')

output = Path.home() / 'SatchelBuild/session/round4-diagnostics-check'
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=output) as temporary:
    folder = Path(temporary)
    with patch('soak_diagnostics.host_sample', side_effect=subprocess.TimeoutExpired('sample', 10)), patch('soak_diagnostics.helper_samples', return_value=helpers):
        write_process_samples(folder, 42, 'watchdog')
    assert json.loads((folder / 'watchdog-host-stacks.json').read_text()) == {'error': 'Symbol sampling timed out after 10 seconds'}
    assert json.loads((folder / 'watchdog-helper-stacks.json').read_text()) == helpers
print('PASS: a host sample timeout still retains owned-helper watchdog evidence')
