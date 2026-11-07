#!/usr/bin/env python3
"""Build and run the synthetic boot feedback and game report checks on the Mac.

SATCHEL_OUT names an output folder outside the checkout. The feedback test runs with a fresh
private home folder (CFFIXED_USER_HOME), so it never touches this Mac's Documents folder."""
from pathlib import Path
import os, shutil, subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ['SATCHEL_OUT']).resolve() / 'feedback-tests'
if OUT.is_relative_to(ROOT): raise SystemExit('Test outputs must be outside the checkout.')
OUT.mkdir(parents=True, exist_ok=True)
app = [ROOT / 'App/NativeGameProfile.swift', ROOT / 'App/NativeGameReport.swift']
for name, sources in [('feedback', [ROOT / 'NativePrepare/FeedbackTests.swift', *app, ROOT / 'App/NativeBootFeedback.swift']),
                      ('report', [ROOT / 'NativePrepare/ReportTests.swift', *app])]:
    binary = OUT / name
    subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-parse-as-library', '-O', '-j', '2', '-num-threads', '2',
                    *map(str, sources), '-o', str(binary)], check=True)
    home = OUT / (name + '-home')
    shutil.rmtree(home, ignore_errors=True); home.mkdir()
    env = dict(os.environ, CFFIXED_USER_HOME=str(home), FEEDBACK_TEST_HOME=str(home))
    subprocess.run([str(binary)], env=env, check=True, timeout=120)
    shutil.rmtree(home)
print('PASS: boot feedback and game report checks')
