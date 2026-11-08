#!/usr/bin/env python3
"""Release dry-run boundaries: tree refusals, offline sandbox, notes and checklist.

Synthetic only. Temporary git repositories stand in for the checkout; nothing is built,
signed or published, and the one socket test connects to loopback inside the sandbox.
"""
import ast
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import release_dry_run as dry  # noqa: E402

NETWORK_MODULES = {'socket', 'ssl', 'http', 'urllib.request', 'ftplib', 'smtplib', 'requests', 'urllib3', 'asyncio'}


class TreeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='satchel-release-')
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name) / 'repo'
        (self.repo / 'scripts').mkdir(parents=True)
        self.hygiene(0)
        (self.repo / '.gitignore').write_text('local/\n*.keys\n')
        (self.repo / 'README.md').write_text('Synthetic\n')
        env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_SYSTEM='/dev/null')
        self.env = patch.dict(os.environ, env)
        self.env.start()
        self.addCleanup(self.env.stop)
        for command in [['init', '-q'], ['add', '.'],
                        ['-c', 'user.name=Synthetic', '-c', 'user.email=synthetic@example.invalid',
                         'commit', '-q', '-m', 'Synthetic']]:
            subprocess.run(['git', '-C', str(self.repo), *command], check=True, capture_output=True)
        root = patch.object(dry, 'ROOT', self.repo)
        root.start()
        self.addCleanup(root.stop)

    def hygiene(self, code):
        (self.repo / 'scripts/check_repo_hygiene.sh').write_text(
            '#!/bin/sh\necho "%s"\nexit %d\n' % ('FAIL: synthetic' if code else 'Repository hygiene checks passed.', code))

    def commit(self):
        subprocess.run(['git', '-C', str(self.repo), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.repo), '-c', 'user.name=Synthetic', '-c',
                        'user.email=synthetic@example.invalid', 'commit', '-q', '-m', 'Synthetic'], check=True)

    def test_clean_tree_passes(self):
        head, hygiene = dry.check_tree()
        self.assertRegex(head, r'^[0-9a-f]{40}$')
        self.assertEqual(hygiene, 'Repository hygiene checks passed.')

    def test_refuses_modified_tracked_file(self):
        (self.repo / 'README.md').write_text('Changed\n')
        with self.assertRaisesRegex(dry.Refused, 'uncommitted or untracked'):
            dry.check_tree()

    def test_refuses_untracked_file(self):
        (self.repo / 'notes.txt').write_text('Untracked\n')
        with self.assertRaisesRegex(dry.Refused, 'uncommitted or untracked'):
            dry.check_tree()

    def test_refuses_failing_hygiene(self):
        self.hygiene(1)
        self.commit()
        with self.assertRaisesRegex(dry.Refused, 'check_repo_hygiene.sh failed'):
            dry.check_tree()

    def test_refuses_ignored_private_files_in_build_folders(self):
        (self.repo / 'App').mkdir()
        (self.repo / 'App/prod.keys').write_text('synthetic, not a key\n')
        with self.assertRaisesRegex(dry.Refused, 'App/prod.keys'):
            dry.check_tree()

    def test_allows_private_folders(self):
        (self.repo / 'local').mkdir()
        (self.repo / 'local/prod.keys').write_text('synthetic, not a key\n')
        dry.check_tree()

    def test_expected_tag_must_match_commit_and_metadata(self):
        # Tags here live only in the temporary synthetic repository.
        expected = dry.distribution.release_layout(
            dry.distribution.release_metadata()['version'], dry.distribution.release_metadata()['build'])['tag']
        head = dry.check_tree()[0]
        with self.assertRaisesRegex(dry.Refused, 'does not point at this commit'):
            dry.check_tag(expected, head)
        with self.assertRaisesRegex(dry.Refused, 'does not match release.json'):
            dry.check_tag(expected + '.9', head)
        subprocess.run(['git', '-C', str(self.repo), 'tag', expected], check=True)
        dry.check_tag(expected, head)
        (self.repo / 'README.md').write_text('Next\n')
        self.commit()
        with self.assertRaisesRegex(dry.Refused, 'does not point at this commit'):
            dry.check_tag(expected, dry.check_tree()[0])

    def test_output_folder_rules(self):
        with self.assertRaisesRegex(dry.Refused, 'outside the checkout'):
            dry.check_out(self.repo / 'out')
        with self.assertRaisesRegex(dry.Refused, 'outside the checkout'):
            dry.check_out(self.repo.parent)
        used = Path(self.temporary.name) / 'used'
        used.mkdir()
        (used / 'old.txt').write_text('old\n')
        with self.assertRaisesRegex(dry.Refused, 'new or empty'):
            dry.check_out(used)
        dry.check_out(Path(self.temporary.name) / 'fresh')

    def test_build_requires_dependency_root(self):
        out = Path(self.temporary.name) / 'out'
        out.mkdir()
        with patch.dict(os.environ, {name: '' for name in dry.DEPENDENCY_VARIABLES}), \
                self.assertRaisesRegex(dry.Refused, 'SATCHEL_DEPS_ROOT'):
            dry.build_here(out, 'synthetic')

    def test_clean_work_must_match_commit(self):
        work = Path(self.temporary.name) / 'work'
        (work / 'logs').mkdir(parents=True)
        (work / 'logs/app.json').write_text(json.dumps({'exit': 0, 'source': 'abc (with local changes)'}))
        out = Path(self.temporary.name) / 'out'
        with patch.object(dry, 'run_logged', return_value=0), self.assertRaisesRegex(dry.Refused, 'not this commit'):
            dry.build_from_clean_work(work, out, 'abc')
        with patch.object(dry, 'run_logged', return_value=1), self.assertRaisesRegex(dry.Refused, '--check does not pass'):
            dry.build_from_clean_work(work, out, 'abc')
        (work / 'logs/app.json').write_text(json.dumps({'exit': 0, 'source': 'abc'}))
        with patch.object(dry, 'run_logged', return_value=0):
            self.assertEqual(dry.build_from_clean_work(work, out, 'abc')[0], work / 'out')


class OfflineTests(unittest.TestCase):
    def test_policy_matches_dependency_recipes(self):
        text = (ROOT / 'scripts/bootstrap_deps.py').read_text()
        self.assertIn("OFFLINE_POLICY = '" + dry.OFFLINE_POLICY + "'", text)

    def test_reexecs_inside_sandbox(self):
        with patch.object(sys, 'argv', ['release_dry_run.py', '--out', '/nonexistent']), \
                patch.object(dry.subprocess, 'call', return_value=0) as call:
            self.assertEqual(dry.main(), 0)
        argv = call.call_args[0][0]
        self.assertEqual(argv[:3], ['/usr/bin/sandbox-exec', '-p', dry.RELEASE_POLICY])
        self.assertEqual(dry.RELEASE_POLICY, dry.OFFLINE_POLICY
                         + ' (allow process-exec (literal "/bin/ps") (with no-sandbox))'
                         + ' (allow process-exec (literal "/usr/bin/sandbox-exec") (with no-sandbox))')
        self.assertEqual(argv[-1], '--network-denied')

    def test_refuses_to_run_unsandboxed(self):
        with patch.object(sys, 'argv', ['release_dry_run.py', '--out', '/nonexistent', '--network-denied']), \
                patch.object(dry, 'sandboxed', return_value=False), self.assertRaises(SystemExit) as stop:
            dry.main()
        self.assertEqual(stop.exception.code, 1)

    def test_sandbox_blocks_ip_and_is_detected(self):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        self.addCleanup(listener.close)
        probe = ('import socket, sys; sys.path.insert(0, %r); import release_dry_run as d\n'
                 'print("sandboxed", d.sandboxed())\n'
                 'try:\n socket.create_connection(("127.0.0.1", %d), timeout=2); print("connected")\n'
                 'except OSError: print("denied")\n') % (str(ROOT / 'scripts'), listener.getsockname()[1])
        result = subprocess.run(['/usr/bin/sandbox-exec', '-p', dry.RELEASE_POLICY, sys.executable, '-B', '-c', probe],
                                capture_output=True, text=True)
        self.assertEqual(result.stdout.split(), ['sandboxed', 'True', 'denied'], result.stderr)

    def test_build_guard_process_listing_runs_in_sandbox(self):
        result = subprocess.run(['/usr/bin/sandbox-exec', '-p', dry.RELEASE_POLICY, '/bin/ps', '-o', 'pid=',
                                 '-p', str(os.getpid())], capture_output=True, text=True)
        self.assertEqual(result.stdout.strip(), str(os.getpid()), result.stderr)
        # Only the two named binaries are excepted.
        self.assertEqual(dry.RELEASE_POLICY.count('no-sandbox'), 2)
        self.assertEqual(dry.EXCEPTIONS, ['/bin/ps', '/usr/bin/sandbox-exec'])

    def test_release_tools_have_no_network_code(self):
        for path in [ROOT / 'scripts/release_dry_run.py', ROOT / 'scripts/build_ipa.py',
                     ROOT / 'NativeApp/Distribution/source.py', ROOT / 'NativeApp/Distribution/schema.py']:
            tree = ast.parse(path.read_text())
            for node in ast.walk(tree):
                names = []
                if isinstance(node, ast.Import):
                    names = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom):
                    names = [node.module or '']
                for name in names:
                    with self.subTest(path=path.name, module=name):
                        self.assertFalse(name in NETWORK_MODULES or name.split('.')[0] in {'http', 'socket', 'ssl'}
                                         or name.startswith('urllib.request'))
                # A command list must never start with a network or publishing tool.
                if isinstance(node, ast.List) and node.elts and isinstance(node.elts[0], ast.Constant):
                    first = str(node.elts[0].value)
                    with self.subTest(path=path.name, command=first):
                        self.assertNotIn(Path(first).name, {'curl', 'wget', 'gh', 'scp', 'rsync', 'ssh', 'nc'})
                if isinstance(node, ast.List) and len(node.elts) > 1 and all(isinstance(e, ast.Constant) for e in node.elts[:2]):
                    if node.elts[0].value == 'git':
                        self.assertNotIn(node.elts[1].value, {'fetch', 'push', 'pull', 'clone', 'tag', 'remote'})


class NotesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='satchel-notes-')
        self.addCleanup(self.temporary.cleanup)
        self.ipa = Path(self.temporary.name) / 'Satchel.ipa'
        self.ipa.write_bytes(b'synthetic archive')
        self.info = {'CFBundleShortVersionString': '1.0', 'CFBundleVersion': '1', 'MinimumOSVersion': '26.0'}
        self.layout = dry.distribution.release_layout('1.0', '1')

    def test_notes_carry_roadmap_compatibility_and_hashes(self):
        notes = dry.release_notes(self.info, self.layout, '0' * 40, self.ipa, 'f' * 64)
        roadmap = dry.table((ROOT / 'ROADMAP.md').read_text(), '## Progress by area')
        games = dry.table((ROOT / 'COMPATIBILITY.md').read_text(), '## Tested games')
        self.assertTrue(roadmap and games)
        for row in roadmap:
            self.assertIn('| ' + row['Area'] + ' | ' + row['Done'] + ' |', notes)
        for row in games:
            self.assertIn(row['Game'], notes)
            self.assertNotIn(row['Title ID'], notes)
        self.assertIn(dry.sha256(self.ipa), notes)
        self.assertIn(self.layout['source'], notes)
        self.assertIn('/blob/v1.0/docs/install.md', notes)
        self.assertTrue(dry.no_dash_punctuation(notes))

    def test_checklist_template_is_complete(self):
        fields = set(re.findall(r'{(\w+)}', dry.CHECKLIST))
        self.assertEqual(fields, {'version', 'build', 'commit', 'date', 'tag', 'repository', 'source_url',
                                  'download_url', 'icon_url', 'source_path', 'size', 'ipa_sha', 'manifest_sha',
                                  'minimum', 'entitlements'})
        self.assertTrue(dry.no_dash_punctuation(dry.CHECKLIST))
        for step in ['Phone gates', 'git push origin', 'gh release create', 'SHA256SUMS', 'Device check']:
            self.assertIn(step, dry.CHECKLIST)

    def test_table_parser_refuses_missing_section(self):
        with self.assertRaises(dry.Refused):
            dry.table('# Nothing\n', '## Progress by area')


if __name__ == '__main__':
    unittest.main(verbosity=2)
