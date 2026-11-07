#!/usr/bin/env python3
"""Which saved shader warm-up resumes by itself: the production WarmupJournal and WarmupResume
from NativePrepare/WarmupJob.swift, with the progress model from WarmupRunner.swift, compiled
for macOS against synthetic journals. Also checks that only the player's controls start a
warm-up (Prepare never does). Usage: run.py <out folder>."""
from pathlib import Path
import re
import runpy
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).expanduser().resolve()
assert not out.is_relative_to(root), 'Keep test products outside the repository'
out.mkdir(parents=True, exist_ok=True)


def between(text, start, end):
    assert text.count(start) == 1 and text.count(end) == 1, (start, end)
    first = text.index(start)
    return text[first:text.index(end, first)]


job = (root / 'NativePrepare/WarmupJob.swift').read_text()
journal = between(job, '// Saved warm-up state. Pure', '// End of saved warm-up state.')
runner = (root / 'NativePrepare/WarmupRunner.swift').read_text()
assert runner.count('\nenum WarmupRunner {') == 1
progress = runner.split('\nenum WarmupRunner {')[0].replace('import Metal\n', '')

# Who starts a warm-up. A run begins only through WarmupJob.begin; start (Warm up, Resume) and
# resumeIfInterrupted lead there. Prepare must call neither, nor run the work itself.
prepare = (root / 'NativePrepare/PrepareJob.swift').read_text()
for call in ['WarmupJob.shared.begin(', 'WarmupJob.perform(', 'WarmupJob.shared.start(', 'resumeIfInterrupted(']:
    assert call not in prepare, 'Prepare must not start a warm-up: ' + call
checked = 0
callers = {}
for folder in ['App', 'NativePrepare', 'NativeHost', 'NativeShaders']:
    for path in sorted((root / folder).rglob('*.swift')):
        text = path.read_text()
        for pattern in [r'\bbegin\(package:', r'\bjob\.start\(package:', r'WarmupJob\.shared\.start\(', r'\bstart\(package: journal']:
            for _ in re.finditer(pattern, text):
                callers.setdefault(str(path.relative_to(root)), []).append(pattern)
        checked += 1
expected = {
    'NativePrepare/WarmupJob.swift': [r'\bbegin\(package:', r'\bbegin\(package:', r'\bstart\(package: journal'],
    'NativePrepare/WarmupView.swift': [r'\bjob\.start\(package:'],
}
for name in expected:
    expected[name].sort(); callers.get(name, []).sort()
assert callers == expected, 'Unexpected warm-up start sites: %r' % callers
print('Warm-up start sites: only the page button and the job itself (%d app sources scanned)' % checked)

# A stop leaves the same words in progress.json (the runner) and the saved job (finish).
stop_runner = between(runner, '        } catch {\n            // The same words the saved job shows', '            try? emit(); throw error')
for word in ['WarmupStopWords.paused', 'WarmupStopWords.stoppedByIOS', 'expired()']:
    assert word in stop_runner, 'The runner must write the shared stop words: ' + word
stop_job = between(job, '    private func finish(', '    private func endBackgroundTask()')
for word in ['WarmupStopWords.paused', 'WarmupStopWords.stoppedByIOS']:
    assert word in stop_job, 'The saved job must use the shared stop words: ' + word
assert 'Warm-up paused.' not in runner and 'was in the background' not in job, 'An old stop wording is left'
assert 'expired: { context.expiry.cancelled }' in job, 'The runner must learn when iOS stopped the run'
print('Stop words: the runner and the saved job share them')

(out / 'main.swift').write_text('import Foundation\n' + progress + '\n' + journal + '\n' + (root / 'Tests/WarmupResume/Tests.swift').read_text())
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2', str(out / 'main.swift'), '-o', str(out / 'warmup-resume-tests')], check=True)
subprocess.run([str(out / 'warmup-resume-tests')], check=True)
