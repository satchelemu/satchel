#!/usr/bin/env python3
"""Regenerate or check the reviewed runtime profile texts against an engine snapshot.

App/NativeRuntimeProfiles.json holds, per reviewed title and version, the exact profile.tsv
that the pinned engine's `game.py profile` writes. App/NativeGameProfiles.json binds each
text by SHA-256. Both must be regenerated when the engine pin moves, because the engine
decides which keys the file carries.

The first line of each text names the game.tsv it was written from. That path is the
maintainer's private profile folder for the title and is part of the hashed text, so the
Mac packager (NativeHost/package.py), which writes profile.tsv from that folder, produces
the same bytes. This tool keeps the recorded path and replaces everything else.

Profile source, first match: NativeGames/profiles/<title>/game.tsv (tracked), the recorded
private folder, the engine's own dm/games/<title>/game.tsv. Only game.tsv is read. It is
profile metadata: no game file is opened or copied.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]
TEXTS = ROOT / 'App/NativeRuntimeProfiles.json'
REGISTRY = ROOT / 'App/NativeGameProfiles.json'
HEAD = re.compile(r'^(# Runtime game profile of .*, written by dm/games/game\.py profile from )(.*)(/game\.tsv)\n')


def generate(engine, title, recorded):
    tracked = ROOT / 'NativeGames/profiles' / title / 'game.tsv'
    source = next((p for p in [tracked, Path(recorded) / 'game.tsv', engine / 'dm/games' / title / 'game.tsv'] if p.is_file()), None)
    if source is None:
        raise SystemExit('No game.tsv found for ' + title)
    with tempfile.TemporaryDirectory() as scratch:
        games = Path(scratch) / 'games'
        (games / title).mkdir(parents=True)
        shutil.copyfile(source, games / title / 'game.tsv')
        out = Path(scratch) / 'profile.tsv'
        env = dict(os.environ, DM_GAME=title, GAME=title, DM_GAMES_FIRST='1', DM_GAMES_DIR=str(games), PYTHONDONTWRITEBYTECODE='1')
        subprocess.run([sys.executable, '-B', str(engine / 'dm/games/game.py'), 'profile', title, str(out)],
                       env=env, cwd=scratch, check=True, stdout=subprocess.DEVNULL)
        text = out.read_text()
    match = HEAD.match(text)
    if not match:
        raise SystemExit('Unexpected profile header for ' + title)
    return match.group(1) + recorded + match.group(3) + '\n' + text[match.end():], source


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--engine', type=Path, required=True, help='Materialized engine snapshot at the pin')
    parser.add_argument('--write', action='store_true', help='Rewrite the two registry files; default is check only')
    args = parser.parse_args()
    engine = args.engine.expanduser().resolve()
    texts = json.loads(TEXTS.read_text())
    registry = json.loads(REGISTRY.read_text())
    changed = False
    for key in texts:
        title, version = key.split(':')
        match = HEAD.match(texts[key])
        if not match:
            raise SystemExit('Unexpected recorded header for ' + key)
        text, source = generate(engine, title, match.group(2))
        digest = hashlib.sha256(text.encode()).hexdigest()
        row = next(r for r in registry if r['titleID'] == title and r['version'] == version)
        same = text == texts[key] and row['profileSHA256'] == digest
        print(('same     ' if same else 'differs  ') + key + ' ' + digest + ' from ' + ('tracked profile' if ROOT in source.parents else 'private or engine profile'))
        if not same:
            changed = True
            texts[key] = text
            row['profileSHA256'] = digest
    if changed and args.write:
        TEXTS.write_text(json.dumps(texts, indent=2, ensure_ascii=False) + '\n')
        REGISTRY.write_text(json.dumps(registry, indent=2, ensure_ascii=False) + '\n')
        print('registry rewritten')
    return 1 if changed and not args.write else 0


if __name__ == '__main__':
    sys.exit(main())
