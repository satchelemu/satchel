"""The public gate must fail closed before shader products are staged."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_receipt
import public_parity as gate


class PublicGateTests(unittest.TestCase):
    def setUp(self):
        parent = Path(os.environ.get('SATCHEL_TEST_OUT', Path.home() / 'SatchelBuild/anygame/parity-tests'))
        parent.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name) / 'repo'
        self.work = Path(self.scratch.name) / 'products'
        for name in ['NativeShaders', 'Tools/MaxwellShaderCompiler', 'Tools/ShaderBridge', 'Tools/CapturedFlatCompiler']:
            (self.root / name).mkdir(parents=True)
        for name in ['NativeShaders/build_receipt.py', 'NativeShaders/build-cross.py',
                     'NativeShaders/build-archive.py', 'NativeShaders/Translation.swift',
                     'Tools/CapturedFlatCompiler/Test.c']:
            (self.root / name).write_text('synthetic source\n')
        groups = [('aot', 'aot', gate.AOT_PRODUCTS)]
        for platform in ('macosx', 'iphoneos'):
            groups += [('cross', 'cross-' + platform, ['libGameShaderBridge.a'] +
                        (['libGameShaderBridge.dylib'] if platform == 'macosx' else [])),
                       ('archive', 'archive-' + platform, ['libShaderArchive.a'])]
        for component, folder, names in groups:
            directory = self.work / folder
            for name in names:
                path = directory / name; path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'synthetic binary')
            value = dict(format=1, component=component, sources=build_receipt.inputs(component, self.root),
                         products={name: build_receipt.digest(directory / name) for name in names})
            (directory / 'build-receipt.json').write_text(json.dumps(value))
        self.lock = dict(format=1, sources=gate.sources(self.root), corpus=gate.corpus_manifest(),
                         outputs={row['name']: {'b': 'synthetic output hash'} for row in gate.corpus()})
        (self.root / gate.LOCK).write_text(json.dumps(self.lock))
        self.proof = dict(format=1, complete=True, differences=0,
                          lock_sha256=build_receipt.digest(self.root / gate.LOCK),
                          products=gate.products(self.work, self.root), outputs=self.lock['outputs'])
        (self.work / 'public-parity').mkdir()
        self.save()

    def save(self):
        (self.work / 'public-parity/result.json').write_text(json.dumps(self.proof))

    def test_valid_bound_proof(self):
        self.assertEqual(gate.validate_proof(self.work, self.root)['cases'], 29)

    def test_changed_swift_source(self):
        (self.root / 'NativeShaders/Translation.swift').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'source checkpoint'):
            gate.validate_proof(self.work, self.root)

    def test_changed_ios_binary(self):
        (self.work / 'aot' / gate.AOT_PRODUCTS[1]).write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'receipt mismatch'):
            gate.validate_proof(self.work, self.root)

    def test_changed_cross_binary(self):
        (self.work / 'cross-iphoneos/libGameShaderBridge.a').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'receipt mismatch'):
            gate.validate_proof(self.work, self.root)

    def test_rebuilt_product_requires_new_corpus_run(self):
        directory = self.work / 'cross-iphoneos'
        (directory / 'libGameShaderBridge.a').write_bytes(b'rebuilt synthetic binary')
        receipt = json.loads((directory / 'build-receipt.json').read_text())
        receipt['products']['libGameShaderBridge.a'] = build_receipt.digest(directory / 'libGameShaderBridge.a')
        (directory / 'build-receipt.json').write_text(json.dumps(receipt))
        with self.assertRaisesRegex(RuntimeError, 'stale or incomplete'):
            gate.validate_proof(self.work, self.root)

    def test_missing_receipt(self):
        (self.work / 'archive-iphoneos/build-receipt.json').unlink()
        with self.assertRaises(FileNotFoundError):
            gate.validate_proof(self.work, self.root)

    def test_incomplete_and_stale_proofs(self):
        for field, value in [('complete', False), ('complete', 1), ('differences', 0.0), ('differences', 1), ('lock_sha256', 'old'),
                             ('outputs', {}), ('products', {})]:
            with self.subTest(field=field):
                original = copy.deepcopy(self.proof)
                self.proof[field] = value; self.save()
                with self.assertRaisesRegex(RuntimeError, 'stale or incomplete'):
                    gate.validate_proof(self.work, self.root)
                self.proof = original

    def test_missing_proof(self):
        (self.work / 'public-parity/result.json').unlink()
        with self.assertRaises(FileNotFoundError):
            gate.validate_proof(self.work, self.root)

    def test_corpus_mutation(self):
        self.lock['corpus'][0]['input_sha256'] = '0' * 64
        (self.root / gate.LOCK).write_text(json.dumps(self.lock))
        with self.assertRaisesRegex(RuntimeError, 'corpus checkpoint'):
            gate.validate_proof(self.work, self.root)

    def test_missing_case(self):
        self.lock['outputs'].pop(next(iter(self.lock['outputs'])))
        (self.root / gate.LOCK).write_text(json.dumps(self.lock))
        with self.assertRaisesRegex(RuntimeError, 'inventory mismatch'):
            gate.validate_proof(self.work, self.root)

    def test_failed_rerun_invalidates_prior_success(self):
        argv = ['public_parity.py', '--products', str(self.work), '--output', str(self.work / 'run')]
        with patch('sys.argv', argv), patch.object(gate, 'sources', return_value={}), \
             patch.object(gate, 'products', return_value={}), patch.object(gate, 'reviewed', return_value={}), \
             patch.object(gate, 'run_corpus', side_effect=RuntimeError('synthetic compiler failure')):
            with self.assertRaisesRegex(RuntimeError, 'synthetic compiler failure'):
                gate.main()
        self.assertFalse(json.loads((self.work / 'public-parity/result.json').read_text())['complete'])
        with self.assertRaisesRegex(RuntimeError, 'stale or incomplete'):
            gate.validate_proof(self.work, self.root)

    def snapshot(self):
        return {str(p.relative_to(self.work)): (p.stat().st_mtime_ns, build_receipt.digest(p))
                for p in sorted(self.work.rglob('*')) if p.is_file()}

    def test_validate_mode_is_read_only(self):
        argv = ['public_parity.py', '--products', str(self.work), '--validate']
        before = self.snapshot()
        with patch('sys.argv', argv), patch.object(gate, 'ROOT', self.root), \
             patch.object(gate, 'run_corpus', side_effect=AssertionError('validate must not run the corpus')):
            gate.main()
        self.assertEqual(self.snapshot(), before)
        # A stale proof fails and stays exactly as it was: validation never marks it incomplete.
        self.proof['lock_sha256'] = 'old'; self.save()
        before = self.snapshot()
        with patch('sys.argv', argv), patch.object(gate, 'ROOT', self.root):
            with self.assertRaisesRegex(RuntimeError, 'stale or incomplete'):
                gate.main()
        self.assertEqual(self.snapshot(), before)

    def test_validate_excludes_candidate_and_needs_no_output(self):
        with patch('sys.argv', ['public_parity.py', '--products', str(self.work), '--validate', '--candidate']):
            with self.assertRaises(SystemExit):
                gate.main()
        with patch('sys.argv', ['public_parity.py', '--products', str(self.work)]):
            with self.assertRaises(SystemExit):
                gate.main()

    def test_staging_rejects_bad_public_or_private_before_copy(self):
        path = gate.ROOT / 'NativeHost/shader_dependency.py'
        spec = importlib.util.spec_from_file_location('shader_dependency', path)
        stage = importlib.util.module_from_spec(spec); spec.loader.exec_module(stage)
        output = Path(self.scratch.name) / 'stage'
        private = Path(self.scratch.name) / 'private.json'
        env = dict(SATCHEL_SHADER_DEPS=str(self.work), SATCHEL_SHADER_SOURCE=str(self.root),
                   SATCHEL_SHADER_PARITY='public', SATCHEL_SHADER_PRIVATE_PROOF=str(private))
        with patch.dict(os.environ, env):
            for proof, message in [(dict(complete=False, differences=0), 'incomplete'),
                                   (dict(complete=True, differences=1), 'has differences')]:
                private.write_text(json.dumps(proof))
                with self.assertRaisesRegex(RuntimeError, message):
                    stage.stage(gate.ROOT, output)
                self.assertFalse(output.exists())
            private.write_text(json.dumps(dict(complete=True, differences=0)))
            self.proof['complete'] = False; self.save()
            with self.assertRaisesRegex(RuntimeError, 'stale or incomplete'):
                stage.stage(gate.ROOT, output)
            self.assertFalse(output.exists())
        with patch.dict(os.environ, dict(env, SATCHEL_SHADER_PARITY='private')):
            private.write_text(json.dumps(dict(complete=False, differences=0)))
            with self.assertRaisesRegex(RuntimeError, 'incomplete'):
                stage.stage(gate.ROOT, output)


if __name__ == '__main__':
    unittest.main()
