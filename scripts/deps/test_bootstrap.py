"""Offline input trust and output provenance checks, using synthetic files only."""
import hashlib
import io
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import json
import errno
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'bootstrap_deps.py'
spec = importlib.util.spec_from_file_location('bootstrap', SCRIPT)
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


def load(name):
    module_spec = importlib.util.spec_from_file_location(name, SCRIPT.parent / (name + '.py'))
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


setup_tools = load('setup_tools')
clean_build = load('clean_build')


class InputTests(unittest.TestCase):
    def test_offline_cache_is_verified_before_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, dest = root / 'source', root / 'dest'
            source.write_bytes(b'synthetic archive')
            pin = hashlib.sha256(source.read_bytes()).hexdigest()
            with patch('subprocess.run', side_effect=AssertionError('Network or process invoked')):
                bootstrap.obtain_pinned(dest, 'https://invalid.example/input', pin, source)
            self.assertEqual(dest.read_bytes(), source.read_bytes())
            source.write_bytes(b'tampered')
            dest.unlink()
            with self.assertRaisesRegex(RuntimeError, 'Pinned SHA-256 mismatch'):
                bootstrap.obtain_pinned(dest, 'https://invalid.example/input', pin, source)
            self.assertFalse(dest.exists())

    def test_missing_pin_cannot_be_downloaded(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('subprocess.run', side_effect=AssertionError('Network invoked')):
                with self.assertRaisesRegex(RuntimeError, 'Missing SHA-256 pin'):
                    bootstrap.obtain_pinned(Path(folder) / 'file', 'https://invalid.example', None, fetch=True)

    def test_missing_offline_input_never_downloads(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('subprocess.run', side_effect=AssertionError('Network invoked')):
                with self.assertRaisesRegex(RuntimeError, 'Missing offline input'):
                    bootstrap.obtain_pinned(Path(folder) / 'file', 'https://invalid.example', '0' * 64)

    def test_existing_file_is_rechecked(self):
        with tempfile.TemporaryDirectory() as folder:
            file = Path(folder) / 'file'
            file.write_bytes(b'incorrect')
            with self.assertRaisesRegex(RuntimeError, 'Pinned SHA-256 mismatch'):
                bootstrap.obtain_pinned(file, 'https://invalid.example', '0' * 64)
            self.assertEqual(file.read_bytes(), b'incorrect')

    def test_bad_download_is_not_promoted(self):
        with tempfile.TemporaryDirectory() as folder:
            file = Path(folder) / 'file'
            def download(command, **kwargs):
                Path(command[-1]).write_bytes(b'incorrect')
            with patch('subprocess.run', side_effect=download):
                with self.assertRaisesRegex(RuntimeError, 'Pinned SHA-256 mismatch'):
                    bootstrap.obtain_pinned(file, 'https://invalid.example', '0' * 64, fetch=True)
            self.assertFalse(file.exists())
            self.assertFalse(file.with_name('file.partial').exists())

    def test_lock_has_no_implicit_pins(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        for row in lock['inputs']:
            self.assertRegex(row['sha256'], r'^[a-f0-9]{64}$')
        for row in lock['inputs'] + lock['tools']:
            self.assertTrue(row['url'].startswith('https://'))
            self.assertTrue(row['license'])
            self.assertFalse(Path(row['destination']).is_absolute())
            self.assertNotIn('..', Path(row['destination']).parts)
        self.assertTrue(all(row['sha256'] is None and row['reason'] for row in lock['gaps']))

    def test_every_tool_has_a_hash_with_named_provenance(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        self.assertEqual({row['id'] for row in lock['tools']},
                         {'cmake', 'xcodegen', 'dotnet-sdk', 'python', 'spirv-headers', 'spirv-tools', 'fonttools'})
        for row in lock['tools']:
            self.assertIn(row['kind'], {'binary', 'source', 'wheel'})
            self.assertTrue(row.get('sha256') or row.get('sha512'))
            if row.get('sha256'):
                self.assertRegex(row['sha256'], r'^[a-f0-9]{64}$')
            if row.get('sha512'):
                self.assertRegex(row['sha512'], r'^[a-f0-9]{128}$')
            self.assertTrue(row['hash_source']['type'])
            self.assertTrue(row['hash_source']['url'].startswith('https://'))
        # Tools are installed by scripts/setup_tools.py, never staged as dependency inputs.
        self.assertEqual(bootstrap.staging_inputs(lock, True), lock['inputs'])
        self.assertEqual(bootstrap.staging_inputs(lock, False), lock['inputs'])

    def test_recipe_gap_blocks_clean_build_even_when_all_sources_are_pinned(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            lock = root / 'lock.json'
            lock.write_text(json.dumps({'inputs': [], 'gaps': [],
                'tool_recipe_gaps': [{'id': 'synthetic-tool', 'reason': 'Not built'}]}))
            with patch.object(bootstrap, 'LOCK', lock), \
                 patch.object(sys, 'argv', [str(SCRIPT), '--root', str(root / 'out')]), \
                 patch('platform.system', return_value='Darwin'), \
                 patch('platform.machine', return_value='arm64'), \
                 patch('subprocess.call', side_effect=AssertionError('Build started')), \
                 redirect_stdout(io.StringIO()) as output:
                self.assertEqual(bootstrap.main(), 2)
            self.assertIn('Unvalidated tool source recipes: synthetic-tool', output.getvalue())
            self.assertFalse((root / 'out').exists())

    def test_pinned_tool_rejects_corrupt_archive_by_either_hash(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / 'tool.tar.gz'
            archive.write_bytes(b'synthetic tool archive')
            sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
            sha512 = hashlib.sha512(archive.read_bytes()).hexdigest()
            self.assertTrue(setup_tools.verified(archive, {'sha256': sha256}))
            self.assertTrue(setup_tools.verified(archive, {'sha512': sha512}))
            self.assertFalse(setup_tools.verified(archive, {'sha256': sha256, 'sha512': '0' * 128}))
            self.assertFalse(setup_tools.verified(archive, {}))
            for row in lock['tools']:
                self.assertFalse(setup_tools.verified(archive, row))

    def test_corrupt_tool_download_is_not_promoted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            row = {'url': 'https://invalid.example/tool.tar.gz', 'destination': 'downloads/tool.tar.gz', 'sha256': '0' * 64}
            def download(command, **kwargs):
                Path(command[command.index('--output') + 1]).write_bytes(b'incorrect')
                return subprocess.CompletedProcess(command, 0)
            with patch('subprocess.run', side_effect=download):
                with self.assertRaisesRegex(RuntimeError, 'Pinned hash mismatch'):
                    setup_tools.obtain(root, row, [], {})
            self.assertEqual(list((root / 'downloads').iterdir()), [])

    def test_tool_folder_must_match_the_lock(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            tools = {row['id']: {'lock': row} for row in lock['tools']}
            tools['cmake'] = {'lock': dict(lock['tools'][0], version='0.0.1')}
            (root / 'tools-manifest.json').write_text(json.dumps({'tools': tools}))
            with self.assertRaisesRegex(RuntimeError, 'does not match the lock'):
                bootstrap.load_tools(root, lock)

    def test_clean_route_requires_pinned_tools(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            lock = root / 'lock.json'
            lock.write_text(json.dumps({'inputs': [], 'gaps': [], 'tool_recipe_gaps': [], 'tools': []}))
            with patch.object(bootstrap, 'LOCK', lock), \
                 patch.object(sys, 'argv', [str(SCRIPT), '--root', str(root / 'out'), '--fetch']), \
                 patch('platform.system', return_value='Darwin'), \
                 patch('platform.machine', return_value='arm64'), \
                 patch('subprocess.call', side_effect=AssertionError('Build started')), \
                 redirect_stderr(io.StringIO()) as errors:
                with self.assertRaises(SystemExit):
                    bootstrap.main()
            self.assertIn('needs --tools', errors.getvalue())
            self.assertFalse((root / 'out').exists())

    def test_isolation_scan_names_foreign_paths_only(self):
        home = '/Users/someone'
        allowed = ['/Users/someone/clone', '/Users/someone/work']
        text = ('/Users/someone/work/deps/m5 /Users/someone/clone/App /usr/bin/clang '
                '/opt/homebrew/bin/spirv-val /Users/someone/SatchelDeps/tmp-snapshot-2026-11-01/x.a')
        self.assertEqual(clean_build.foreign_paths(text, allowed, home),
                         {'/opt/homebrew', '/Users/someone/SatchelDeps/tmp-snapshot-2026-11-01/x.a'})

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS Sandbox profile')
    def test_offline_policy_allows_local_ipc_but_denies_ip_connections(self):
        code = (
            'import socket,tempfile,pathlib,json; t=tempfile.TemporaryDirectory(); '
            's=socket.socket(socket.AF_UNIX); s.bind(str(pathlib.Path(t.name)/"fixture.sock")); '
            'print(json.dumps({"ip_result":socket.socket().connect_ex(("127.0.0.1",9))}))'
        )
        result = subprocess.run(['/usr/bin/sandbox-exec', '-p', bootstrap.OFFLINE_POLICY,
                                 sys.executable, '-c', code], check=True, text=True, capture_output=True)
        self.assertEqual(json.loads(result.stdout)['ip_result'], errno.EPERM)

    def test_archive_cannot_escape_output(self):
        import io
        import tarfile
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root / 'synthetic.tar'
            with tarfile.open(archive, 'w') as stream:
                info = tarfile.TarInfo('../escaped')
                info.size = 4
                stream.addfile(info, io.BytesIO(b'test'))
            with self.assertRaises(tarfile.OutsideDestinationError):
                bootstrap.extract(archive, root / 'out')
            self.assertFalse((root / 'escaped').exists())


if __name__ == '__main__':
    unittest.main()
