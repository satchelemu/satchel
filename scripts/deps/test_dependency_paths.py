"""Default dependency locations: explicit variables win, stale roots stop with the variable to set."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'dependency_paths.py'
spec = importlib.util.spec_from_file_location('dependency_paths', SCRIPT)
paths = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paths)
VARIABLES = ['SATCHEL_DEPS_ROOT', 'SATCHEL_NATIVE_BUILD_INPUTS', 'SATCHEL_SHADER_DEPS',
             'SATCHEL_ARCHIVE_LIBRARY', 'SATCHEL_MAC_HOST_GEN']


class DependencyPathTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.home = Path(scratch.name) / 'SatchelDeps'
        clean = {k: v for k, v in os.environ.items() if k not in VARIABLES}
        for patcher in [patch.dict(os.environ, clean, clear=True), patch.object(paths, 'DEFAULT_ROOT', self.home)]:
            patcher.start(); self.addCleanup(patcher.stop)

    def materialize(self, root, pin):
        (root / 'NX2Apple').mkdir(parents=True, exist_ok=True)
        (root / 'NX2Apple/engine-materialization.json').write_text(json.dumps({'pin': pin, 'patches': []}))

    def test_pin_is_the_tracked_queue_reference(self):
        self.assertRegex(paths.pin(), r'^[0-9a-f]{40}$')
        host = (SCRIPT.parents[1] / 'NativeHost/build.py').read_text()
        self.assertIn(f"PIN = '{paths.pin()}'", host)

    def test_default_root_without_materialization_stops(self):
        self.home.mkdir()
        with self.assertRaisesRegex(SystemExit, 'SATCHEL_DEPS_ROOT is not set.*records no engine materialization'):
            paths.engine()

    def test_default_root_at_an_older_pin_stops(self):
        self.materialize(self.home, 'bbc3b73784751d2a706932dc69fd67627e3445d4')
        for resolve in [paths.deps_root, paths.engine, paths.build_inputs, paths.shader_deps, paths.integration_tools]:
            with self.subTest(resolve=resolve.__name__):
                with self.assertRaisesRegex(SystemExit, 'records engine bbc3b737, not the pinned engine ' + paths.pin()[:8]):
                    resolve()

    def test_default_root_at_the_pin_is_used(self):
        self.materialize(self.home, paths.pin())
        self.assertEqual(paths.engine(), self.home / 'NX2Apple/engine')
        self.assertEqual(paths.build_inputs(), self.home / 'build-inputs')
        self.assertEqual(paths.shader_deps(), self.home / 'm5')

    def test_explicit_variables_win(self):
        self.materialize(self.home, 'bbc3b73784751d2a706932dc69fd67627e3445d4')
        root = self.home.parent / 'pinned'
        with patch.dict(os.environ, SATCHEL_DEPS_ROOT=str(root)):
            self.assertEqual(paths.engine(), root / 'NX2Apple/engine')
            self.assertEqual(paths.build_inputs(), root / 'build-inputs')
            self.assertEqual(paths.shader_deps(), root / 'm5')
        with patch.dict(os.environ, SATCHEL_NATIVE_BUILD_INPUTS='/inputs', SATCHEL_SHADER_DEPS='/shaders'):
            self.assertEqual(paths.build_inputs(), Path('/inputs'))
            self.assertEqual(paths.shader_deps(), Path('/shaders'))

    def test_archive_library(self):
        products = self.home.parent / 'm5'
        with patch.dict(os.environ, SATCHEL_SHADER_DEPS=str(products)):
            with self.assertRaisesRegex(SystemExit, 'set SATCHEL_SHADER_DEPS'):
                paths.archive_library('macosx')
            library = products / 'archive-macosx/libShaderArchive.a'
            library.parent.mkdir(parents=True); library.write_bytes(b'synthetic')
            self.assertEqual(paths.archive_library('macosx'), library)
            with patch.dict(os.environ, SATCHEL_ARCHIVE_LIBRARY=str(products / 'missing.a')):
                with self.assertRaisesRegex(SystemExit, 'the path SATCHEL_ARCHIVE_LIBRARY names'):
                    paths.archive_library('macosx')

    def test_mac_host_gen(self):
        with patch.dict(os.environ, SATCHEL_NATIVE_BUILD_INPUTS=str(self.home.parent / 'inputs')):
            with self.assertRaisesRegex(SystemExit, 'set SATCHEL_MAC_HOST_GEN'):
                paths.mac_host_gen()
            (self.home.parent / 'inputs/host-gen').mkdir(parents=True)
            self.assertEqual(paths.mac_host_gen(), self.home.parent / 'inputs/host-gen')

    def test_command_line(self):
        env = dict(os.environ, SATCHEL_DEPS_ROOT='/pinned-root')
        out = subprocess.run([sys.executable, '-B', SCRIPT, 'engine'], env=env, capture_output=True, text=True, check=True)
        self.assertEqual(out.stdout.strip(), '/pinned-root/NX2Apple/engine')
        failed = subprocess.run([sys.executable, '-B', SCRIPT, 'archive-library', 'macosx'], env=env, capture_output=True, text=True)
        self.assertEqual(failed.returncode, 1)
        self.assertIn('set SATCHEL_SHADER_DEPS', failed.stderr)


if __name__ == '__main__':
    unittest.main()
