#!/usr/bin/env python3
"""Satchel's orientation contract, in every place an app or fixture declares it.

Adapted from nxnative's dm/ios/tests/test_orientation_policy.py (MIT, upstream e2fadf22).
Upstream lets an iPad take all four orientations; Satchel is an iPhone app that stays in
landscape (AGENTS.md) on both Duo screens, so every declaration must be exactly landscape
left and right, full screen, with no portrait or iPad override. The single exception is made
at run time, in one place: a game on screen on a half-folded Duo turns upright
(NativeHost/DisplayControls.m SatchelGameOrientationMask), because the hinge crosses the
long side of the inner screen. The app, the pause menu and the harness only repeat that answer.
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
        # No app or host source asks the system for an upright scene: a half-folded game is
        # turned by the host inside the landscape scene (DisplayControls.h).
        for name in ['App', 'NativeHost']:
            for path in sorted((ROOT / name).rglob('*')):
                if path.suffix in {'.swift', '.m', '.h'}:
                    self.assertNotRegex(path.read_text(errors='replace'), r'UIInterfaceOrientationMaskPortrait|UIInterfaceOrientationMaskAll|: \.portrait\b|\.allButUpsideDown', str(path))

    def test_upright_rule(self):
        # The host's rule: upright only for a game on screen while half folded, and the turn is
        # a quarter turn of the game's window, never a request to the system.
        host = (ROOT / 'NativeHost/DisplayControls.m').read_text()
        self.assertRegex(host, r'BOOL upright = orientationFollowsPosture && onScreen && effectivePosture\(\) == 2;')
        self.assertEqual(host.count('CGAffineTransformMakeRotation('), 1, 'one place turns the window')
        self.assertNotIn('requestGeometryUpdateWithPreferences', host)

    def test_native_fixtures(self):
        for name in ['NativeHost/display/simulator.py', 'Tests/Duo/run.py']:
            text = (ROOT / name).read_text()
            match = re.search(r"'UISupportedInterfaceOrientations': ?\[([^\]]*)\]", text)
            self.assertIsNotNone(match, name)
            self.assertEqual(set(re.findall(r"'(UIInterfaceOrientation\w+)'", match[1])), LANDSCAPE, name)


if __name__ == '__main__':
    unittest.main()
