#!/usr/bin/env python3
"""Reviewed host profiles shared by the packager and the app."""
import json
from pathlib import Path
REGISTRY = Path(__file__).resolve().parents[1] / 'App/NativeGameProfiles.json'
def profiles():
    return json.loads(REGISTRY.read_text())
def profile(title, version):
    matches = [p for p in profiles() if p['titleID'] == title.upper() and p['version'] == version]
    if len(matches) != 1:
        raise ValueError('This title has no supported profile yet. Your imported file is safe.')
    return matches[0]
