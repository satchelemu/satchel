#!/usr/bin/env python3
"""Which saved shader warm-up resumes by itself: the production WarmupJournal and WarmupResume
from NativePrepare/WarmupJob.swift, with the progress model from WarmupRunner.swift, compiled
for macOS against synthetic journals. Also checks that only the player's controls start a
warm-up (Prepare never does), and compiles the whole production WarmupJob.swift against Mac
stand-ins (GuardStubs.swift) to drive every path that opens a game (Guard.swift): a warm-up
never starts, resumes or keeps running beside a game that is open or on its way.
Usage: run.py <out folder> [scenario ...]."""
from pathlib import Path
import os
import re
import runpy
import shutil
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

# The game guard: the whole job, unchanged but for its imports, with the app's own
# PreparedLaunch, NativePreparedCycle.requested and PrepareJob.gameSessionOpen.
launch = between((root / 'NativeHost/PreparedGame.swift').read_text(),
                 '/// The package currently owned by the native session coordinator.', '/// The background time around the package check')
cycle = between((root / 'NativeHost/SatchelMenuSession.swift').read_text(), '    static var requested: Bool {', '    static func shouldLaunch(')
session_open = re.findall(r'^ *static var gameSessionOpen: Bool \{.*\}$', prepare, re.M)
assert len(session_open) == 1, 'PrepareJob.gameSessionOpen must stay one line'
imports = ['import BackgroundTasks\n', 'import CryptoKit\n', 'import SwiftUI\n']
for line in imports:
    assert job.count(line) == 1, line
body = job
for line in imports:
    body = body.replace(line, '')
stubs = (root / 'Tests/WarmupResume/GuardStubs.swift').read_text().replace('    __GAME_SESSION_OPEN__', session_open[0])
(out / 'guard').mkdir(exist_ok=True)
(out / 'guard/main.swift').write_text('import Foundation\nimport Combine\nimport CryptoKit\n' + progress + '\n' + stubs + '\n' + launch
    + '\n@MainActor enum NativePreparedCycle {\n' + cycle + '}\n' + body + '\n' + (root / 'Tests/WarmupResume/Guard.swift').read_text())
name = 'satchel-warmup-guard-tests'
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2', str(out / 'guard/main.swift'), '-o', str(out / 'guard' / name)], check=True)
developer = ['-SatchelDeveloperControls', 'YES']
scenarios = {
    'automatic-launch': developer + ['-SatchelAutoLaunchPreparedGame', 'YES', '-SatchelPreparedGame', 'Prepared-Synthetic'],
    'play': [], 'play-pauses-running': [], 'session-without-request': [], 'pause-menu-library': [],
    'developer-cycle': developer + ['-SatchelCyclePreparedGames', 'Prepared-Synthetic,Prepared-Other',
                                    '-SatchelCycleSeconds', '1', '-SatchelCycleRounds', '1'],
    'return-while-starting': [], 'ios-stop-while-game-opens': [], 'opt-in': [],
}
chosen = sys.argv[2:] or list(scenarios)
failed = []
try:
    for scenario in chosen:
        home = out / 'guard/home' / scenario
        shutil.rmtree(home, ignore_errors=True); home.mkdir(parents=True)
        # CFFIXED_USER_HOME keeps the synthetic Documents folder inside the out folder.
        result = subprocess.run([str(out / 'guard' / name), scenario] + scenarios[scenario],
                                env={**os.environ, 'CFFIXED_USER_HOME': str(home)}, timeout=120)
        if result.returncode: failed.append(scenario)
finally:
    # The job saves its journal in user defaults; the test binary's own domain only.
    Path.home().joinpath('Library/Preferences', name + '.plist').unlink(missing_ok=True)
assert not failed, 'Warm-up guard scenarios failed: ' + ', '.join(failed)
print('Warm-up guard: %d scenarios passed' % len(chosen))
