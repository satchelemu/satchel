#!/usr/bin/env python3
"""Synthetic tests for mutation boundaries, clean-break identities, and repeated application."""
import contextlib
import importlib.util
import io
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('rename', Path(__file__).with_name('rename_to_satchel.py'))
rename = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rename)


class RenameTests(unittest.TestCase):
    def test_names_and_abi(self):
        source = ('PocketLab Pocket Lab POCKET LAB pocketlab PocketLabDev PocketCPUProbe '
                  'POCKET_UI_PREVIEW_MINIMAL pocket_shader_runtime PLNativeSession '
                  '-PocketPreparedGame PocketLiveControls PocketPlayerSettings.title '
                  'pocket_game_container_v1 @rpath/PocketPreparedGame.dylib '
                  'com.dell.pocketlab.native.title group.com.dell.pocketlab.migration '
                  'PocketLab-report.json PocketLab-validation.json')
        result = rename.transform(source)
        self.assertEqual(result, 'Satchel Satchel SATCHEL satchel SatchelDev SatchelCPUProbe '
                         'SATCHEL_UI_PREVIEW_MINIMAL satchel_shader_runtime SatchelNativeSession '
                         '-SatchelPreparedGame SatchelLiveControls SatchelPlayerSettings.title '
                         'satchel_game_container_v1 @rpath/SatchelPreparedGame.dylib '
                         'com.dell.satchel.native.title group.com.dell.satchel.migration '
                         'Satchel-report.json Satchel-validation.json')
        self.assertFalse(rename.remaining(result))
        self.assertEqual(rename.transform(result), result)

    def test_new_main_prefixes(self):
        self.assertEqual(rename.transform('pl_phone_shader_prefetch pl_ghost_pool PL_SHADER_TIME'),
                         'satchel_phone_shader_prefetch satchel_ghost_pool SATCHEL_SHADER_TIME')
        self.assertEqual(rename.transform('pl_clock_change pl_fs_open_beneath pl_round8_dispatch -DPL_FS_EXTERNAL_GUEST=1 PL_TIME_NOT_IMPLEMENTED'),
                         'satchel_clock_change satchel_fs_open_beneath satchel_round8_dispatch -DSATCHEL_FS_EXTERNAL_GUEST=1 SATCHEL_TIME_NOT_IMPLEMENTED')

    def tree(self, files):
        temp = tempfile.TemporaryDirectory(prefix='rename-fixture-', dir=os.environ.get('TMPDIR'))
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        subprocess.run(['git', 'init', '-q', root], check=True)
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        subprocess.run(['git', '-C', root, 'add', '.'], check=True)
        return root

    def run_tool(self, root, *args):
        return subprocess.run(['python3', '-B', str(Path(rename.__file__)), '--root', root, *args],
                              capture_output=True, text=True)

    def test_apply_and_idempotence(self):
        root = self.tree({'PocketLab.xcodeproj/PocketLab.txt': b'PocketLab -PocketPreparedGame',
                          'Validation/history/Pocket.txt': b'Pocket history',
                          'vendor/Pocket.h': b'Pocket upstream', 'Reference-LICENSE.txt': b'Pocket notice'})
        self.assertEqual(self.run_tool(root).returncode, 0)
        self.assertTrue((root / 'PocketLab.xcodeproj/PocketLab.txt').exists())
        self.assertEqual(self.run_tool(root, '--check').returncode, 1)
        first = self.run_tool(root, '--apply')
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        self.assertEqual((root / 'Satchel.xcodeproj/Satchel.txt').read_text(), 'Satchel -SatchelPreparedGame')
        self.assertFalse((root / 'PocketLab.xcodeproj').exists())
        self.assertEqual((root / 'Validation/history/Pocket.txt').read_bytes(), b'Pocket history')
        self.assertEqual((root / 'vendor/Pocket.h').read_bytes(), b'Pocket upstream')
        self.assertEqual((root / 'Reference-LICENSE.txt').read_bytes(), b'Pocket notice')
        self.assertEqual(self.run_tool(root, '--check').returncode, 0)
        self.assertIn('APPLIED: 0 files', self.run_tool(root, '--apply').stdout)

    def test_ambiguity_refuses_without_writes(self):
        for name, data in [('mixed.txt', b'pOcKeT'), ('binary.dat', b'\0PocketLab'), ('wide.strings', 'PocketLab'.encode('utf-16'))]:
            with self.subTest(name=name):
                root = self.tree({'normal.txt': b'PocketLab', name: data})
                result = self.run_tool(root, '--apply')
                self.assertEqual(result.returncode, 1)
                self.assertEqual((root / 'normal.txt').read_bytes(), b'PocketLab')
                self.assertIn('no files written', result.stdout)

    def test_exports_receive_new_abi(self):
        before = 'int pocket_shader_probe(const void *p, int n);\n'
        after = rename.transform_file('probe.h', before)
        self.assertEqual(after, 'int satchel_shader_probe(const void *p, int n);\n')
        self.assertEqual(rename.transform_file('probe.h', after), after)
        self.assertEqual(rename.transform('[UnmanagedCallersOnly(EntryPoint="pocket_shader_probe")]'),
                         '[UnmanagedCallersOnly(EntryPoint="satchel_shader_probe")]')
        self.assertEqual(rename.transform('dlsym(handle, "pocket_new_abi")'),
                         'dlsym(handle, "satchel_new_abi")')

    def test_policy_rewrites(self):
        agents = '# Pocket Lab\n\n## Name\nThe bundle ID `com.dell.pocketlab` never changes.\n\n## Direction\nKeep this.\n'
        result = rename.transform_file('AGENTS.md', agents)
        self.assertNotIn('never changes', result)
        self.assertIn('clean break', result)
        self.assertIn('Keep this.', result)
        self.assertEqual(rename.transform_file('AGENTS.md', result), result)
        plan = '## Rename to Satchel\nKeep: bundle ID `com.dell.pocketlab`\n## Next\nKeep this.\n'
        result = rename.transform_file('PLAN.md', plan)
        self.assertIn('## Next\nKeep this.', result)
        self.assertNotIn('Keep: bundle ID', result)
        self.assertEqual(rename.transform_file('PLAN.md', result), result)

    def test_unrelated_pl_stays(self):
        self.assertEqual(rename.transform('PLAN PLIST PLT PLATFORM_IOS PLUnknownThirdParty PLAYERS PLUS DM_PLAYERS B_PLUS KEY_PLUS'),
                         'PLAN PLIST PLT PLATFORM_IOS PLUnknownThirdParty PLAYERS PLUS DM_PLAYERS B_PLUS KEY_PLUS')
        self.assertEqual(rename.transform('PLNativeSessionDidEnd pl_cpu_probe PL_CPU_PROBE_H'),
                         'SatchelNativeSessionDidEnd satchel_cpu_probe SATCHEL_CPU_PROBE_H')

    def test_source_locks_keep_provenance_digests(self):
        old = b'PocketCPUProbe'
        lock = json.dumps({'source.c': hashlib.sha256(old).hexdigest(), 'external': 'a' * 64}).encode()
        root = self.tree({'source.c': old, 'source-lock.json': lock})
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = json.loads((root / 'source-lock.json').read_text())
        self.assertEqual(data['source.c'], hashlib.sha256(old).hexdigest())
        self.assertEqual(data['external'], 'a' * 64)
        self.assertEqual(self.run_tool(root, '--check').returncode, 0)

    def test_compiler_define_prefix(self):
        self.assertEqual(rename.transform('-Dpl_guest_dispatch_enable_vulkan_' + "' + old"),
                         '-Dsatchel_guest_dispatch_enable_vulkan_' + "' + old")
        self.assertEqual(rename.transform('-DPL_SWITCH_EVENTS_TESTING -UPL_CPU_PROBE_H'),
                         '-DSATCHEL_SWITCH_EVENTS_TESTING -USATCHEL_CPU_PROBE_H')

    def test_generated_symbols_and_internal_words(self):
        self.assertEqual(rename.transform('"int pl_" + name + "_session_reset"'),
                         '"int satchel_" + name + "_session_reset"')
        self.assertEqual(rename.transform('_pl_cpu_probe SATCHEL_STORAGE_PLAN_OK SATCHEL_SWITCH_IPC_SERVICE_PL_S'),
                         '_satchel_cpu_probe SATCHEL_STORAGE_PLAN_OK SATCHEL_SWITCH_IPC_SERVICE_PL_S')

    def test_unknown_compiler_prefix_is_reported(self):
        root = self.tree({'build.sh': b'cc -DPLUnknownVendor'})
        result = self.run_tool(root)
        self.assertEqual(result.returncode, 0)
        self.assertIn('PL kept: PLUnknownVendor', result.stdout)
        self.assertEqual((root / 'build.sh').read_bytes(), b'cc -DPLUnknownVendor')

    def test_binary_filename_only(self):
        root = self.tree({'PocketLab.png': b'\x89PNG\0\xff'})
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((root / 'Satchel.png').read_bytes(), b'\x89PNG\0\xff')
        self.assertIn('APPLIED: 0 files', self.run_tool(root, '--apply').stdout)

    def test_reviewed_runtime_binding_updates_and_refuses_drift(self):
        value = '# Pocket Lab runtime\n'
        before = hashlib.sha256(value.encode()).hexdigest()
        root = self.tree({'App/NativeRuntimeProfiles.json': json.dumps({'T:1': value}).encode(),
                          'App/NativeGameProfiles.json': json.dumps([{'titleID': 'T', 'version': '1', 'profileSHA256': before}]).encode()})
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        row = json.loads((root / 'App/NativeGameProfiles.json').read_text())[0]
        self.assertEqual(row['profileSHA256'], hashlib.sha256(b'# Satchel runtime\n').hexdigest())
        self.assertIn('APPLIED: 0 files', self.run_tool(root, '--apply').stdout)
        (root / 'App/NativeRuntimeProfiles.json').write_text(json.dumps({'T:1': 'altered'}))
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Reviewed runtime profile digest mismatch', result.stdout)
        self.assertEqual(json.loads((root / 'App/NativeGameProfiles.json').read_text())[0], row)

    def test_runtime_binding_refuses_symlink_input(self):
        root = self.tree({'App/NativeGameProfiles.json': b'[]', 'outside.json': b'{}'})
        (root / 'App/NativeRuntimeProfiles.json').symlink_to('../outside.json')
        subprocess.run(['git', '-C', root, 'add', 'App/NativeRuntimeProfiles.json'], check=True)
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Runtime profile binding needs review: symlink input', result.stdout)
        self.assertEqual((root / 'outside.json').read_bytes(), b'{}')

    def test_reviewed_source_binding_rejects_drift(self):
        rows = json.loads((rename.HERE / 'reviewed-bindings.json').read_text())
        own = rename.HERE.parents[1]
        manifest = 'Tools/ScalarCompiler/source-lock.json'
        selected = [row for row in rows if row['manifest'] == manifest]
        files = {manifest: (own / manifest).read_bytes()}
        files.update({row['source']: (own / row['source']).read_bytes() for row in selected})
        root = self.tree(files)
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('APPLIED: 0 files', self.run_tool(root, '--apply').stdout)
        path = root / selected[0]['source']
        path.write_bytes(path.read_bytes() + b'\n/* drift */\n')
        result = self.run_tool(root, '--apply')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Reviewed source changed, re-review binding', result.stdout)

    def test_collision_refuses(self):
        root = self.tree({'PocketLab.txt': b'PocketLab', 'Satchel.txt': b'Keep me'})
        self.assertEqual(self.run_tool(root, '--apply').returncode, 1)
        self.assertEqual((root / 'Satchel.txt').read_bytes(), b'Keep me')

    def test_untracked_not_touched(self):
        root = self.tree({'normal.txt': b'PocketLab'})
        (root / 'private.txt').write_text('PocketLab')
        self.assertEqual(self.run_tool(root, '--apply').returncode, 0)
        self.assertEqual((root / 'private.txt').read_text(), 'PocketLab')

    def test_symlink_target_not_followed(self):
        root = self.tree({'normal.txt': b'PocketLab'})
        target = root.parent / (root.name + '-Pocket-outside')
        target.write_text('PocketLab')
        self.addCleanup(target.unlink)
        (root / 'link').symlink_to(target)
        subprocess.run(['git', '-C', root, 'add', 'link'], check=True)
        result = self.run_tool(root, '--apply')
        # A link through the old build-root spelling requires explicit review.
        self.assertEqual(result.returncode, 1)
        self.assertIn('Symlink target needs review', result.stdout)
        self.assertEqual(target.read_text(), 'PocketLab')
        self.assertEqual(os.readlink(root / 'link'), str(target))


if __name__ == '__main__':
    unittest.main()
