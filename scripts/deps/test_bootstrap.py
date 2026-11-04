"""Offline input trust and output provenance checks, using synthetic files only."""
import hashlib
import io
from contextlib import redirect_stdout
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
        for row in lock['inputs'] + lock.get('tool_sources', []):
            self.assertRegex(row['sha256'], r'^[a-f0-9]{64}$')
            self.assertTrue(row['url'].startswith('https://'))
            self.assertTrue(row['license'])
            self.assertFalse(Path(row['destination']).is_absolute())
            self.assertNotIn('..', Path(row['destination']).parts)
        self.assertTrue(lock['gaps'])
        self.assertTrue(all(row['sha256'] is None and row['reason'] for row in lock['gaps']))

    def test_tool_source_pins_are_not_claimed_as_exercised_inputs(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        self.assertEqual(bootstrap.staging_inputs(lock, False), lock['inputs'])
        staged = bootstrap.staging_inputs(lock, True)
        self.assertEqual(len({row['id'] for row in staged}), len(staged))
        self.assertEqual({row['id'] for row in lock['tool_sources']},
                         {row['id'] for row in lock['tool_recipe_gaps']})
        for row in lock['tool_sources']:
            self.assertIn(row, staged)
            self.assertEqual(row['provenance']['type'], 'installed-formula-source-pin')
            self.assertRegex(row['provenance']['metadata_sha256'], r'^[a-f0-9]{64}$')
            self.assertIn('Archive bytes unavailable locally', row['verification'])

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

    def test_pinned_tool_source_rejects_corrupt_offline_archive(self):
        lock = json.loads(bootstrap.LOCK.read_text())
        row = lock['tool_sources'][0]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            cached = root / row['cache']
            cached.write_bytes(b'synthetic corrupt tool source')
            with patch('subprocess.run', side_effect=AssertionError('Network invoked')):
                with self.assertRaisesRegex(RuntimeError, 'Pinned SHA-256 mismatch'):
                    bootstrap.obtain_pinned(root / 'out', row['url'], row['sha256'], cached)
            self.assertFalse((root / 'out').exists())

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
