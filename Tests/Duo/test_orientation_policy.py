#!/usr/bin/env python3
"""Satchel's orientation contract, in every place an app or fixture declares it.

Adapted from nxnative's dm/ios/tests/test_orientation_policy.py (MIT, upstream e2fadf22).
Upstream lets an iPad take all four orientations; Satchel is an iPhone app that stays in
landscape (AGENTS.md), on both Duo screens and through the hinge's postures, so every
declaration must be exactly landscape left and right, full screen, with no portrait or
iPad override, and the app's runtime lock must match.
"""
from pathlib import Path
import json
import plistlib
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
LANDSCAPE = {'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'}


def yaml(path):
    return json.loads(subprocess.check_output(['ruby', '-rjson', '-ryaml', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', str(path)], text=True))


class OrientationPolicy(unittest.TestCase):
    def check(self, where, properties):
        with self.subTest(where=where):
            self.assertEqual(set(properties.get('UISupportedInterfaceOrientations', [])), LANDSCAPE)
            self.assertNotIn('UISupportedInterfaceOrientations~ipad', properties)
            self.assertIs(properties.get('UIRequiresFullScreen'), True)

    def test_app_info_plist(self):
        self.check('App/Info.plist', plistlib.loads((ROOT / 'App/Info.plist').read_bytes()))

    def test_projects(self):
        checked = 0
        for name in ['project.yml', 'Tests/UIPreview/project-game.yml', 'Tests/UIPreview/project.yml']:
            project = yaml(ROOT / name)
            for target, spec in project['targets'].items():
                properties = spec.get('info', {}).get('properties')
                settings = spec.get('settings', {}).get('base', {})
                if properties and 'UISupportedInterfaceOrientations' in properties:
                    self.check(f'{name}:{target}', properties); checked += 1
                elif 'INFOPLIST_KEY_UISupportedInterfaceOrientations' in settings:
                    with self.subTest(where=f'{name}:{target}'):
                        self.assertEqual(set(settings['INFOPLIST_KEY_UISupportedInterfaceOrientations'].split()), LANDSCAPE)
                    checked += 1
        self.assertGreaterEqual(checked, 3, 'the app and both preview projects declare their orientations')

    def test_runtime_lock(self):
        # GameOrientation.swift: the scene lock and every controller's mask stay landscape.
        source = (ROOT / 'App/GameOrientation.swift').read_text()
        self.assertRegex(source, r'static let supportedMask: UIInterfaceOrientationMask = \.landscape\n')
        self.assertNotRegex(source, r'\.portrait|\.allButUpsideDown|\.all\b')

    def test_native_fixtures(self):
        for name in ['NativeHost/display/simulator.py', 'Tests/Duo/run.py']:
            text = (ROOT / name).read_text()
            match = re.search(r"'UISupportedInterfaceOrientations': ?\[([^\]]*)\]", text)
            self.assertIsNotNone(match, name)
            self.assertEqual(set(re.findall(r"'(UIInterfaceOrientation\w+)'", match[1])), LANDSCAPE, name)


if __name__ == '__main__':
    unittest.main()
