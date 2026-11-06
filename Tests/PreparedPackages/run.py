#!/usr/bin/env python3
"""Stale and replaced prepared packages: the real verifier, library scan, shelf state and
replacement cleanup, compiled for macOS against synthetic packages only."""
from pathlib import Path
import runpy
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).expanduser().resolve()
assert not out.is_relative_to(root), 'Keep test products outside the repository'
out.mkdir(parents=True, exist_ok=True)


def between(text, start, end):
    assert text.count(start) == 1 and text.count(end) >= 1, (start, end)
    first = text.index(start)
    return text[first:text.index(end, first)]


documents = 'testRoot.appendingPathComponent("Documents")'
# The verifier, package deletion and replacement cleanup, exactly as the app builds them,
# with only the iOS session bridge stubbed and storage redirected into this test.
verifier = (root / 'NativeHost/PreparedGame.swift').read_text().split('\n/// The background time around the package check')[0]
verifier = verifier.replace('import SwiftUI', 'import Foundation\nimport SwiftUI')
verifier = verifier.replace('FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]', 'testRoot.appendingPathComponent("Support")')
verifier = verifier.replace('fm.urls(for: .documentDirectory, in: .userDomainMask)[0]', documents)
verifier = verifier.replace('FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]', documents)
verifier += '\nvar sessionStarted = false\nfunc SatchelNativeSessionStarted() -> Bool { sessionStarted }\n'
# The library's package scan, Play choice and shelf state, around a stub artwork store.
artwork = (root / 'App/GameArtwork.swift').read_text()
scan = between(artwork, '/// A package prepared for native play', 'extension Notification.Name')
scan = scan.replace('FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]', documents)
choice = between(artwork, '    func isPrepared(', '    /// Loads cached entries')
shelf = (root / 'App/GameShelf.swift').read_text()
play = between(shelf, 'enum NativePlay {', '/// One tile on the shelf')
states = between(shelf, '/// Where a library entry', 'extension GameArtworkStore {')
state = between(shelf, '    func state(', '    func title(')
library = scan + play + states + '''
enum SatchelTheme { static let leaf = Color.green, spark = Color.yellow, tide = Color.blue, textDim = Color.gray, violet = Color.purple }
struct GameInfo { enum Kind { case application, update, addOnContent }; let applicationID: UInt64; let kind: Kind; var displayVersion = "" }
final class GameArtworkStore {
    enum Status: Equatable { case idle, reading, needsKeys, failed(String) }
    var prepared: [PreparedGame] = []
    var infos: [String: GameInfo] = [:]
    var status: [String: Status] = [:]
    var keysAvailable = true
    func info(_ item: HomebrewItem) -> GameInfo? { infos[item.id] }
    private func loadTitleArt() {}
''' + choice + '}\nextension GameArtworkStore {\n' + state + '}\n'
(out / 'main.swift').write_text(verifier + '\n' + library + '\n' + (root / 'Tests/PreparedPackages/Tests.swift').read_text())
runpy.run_path(str(root / 'NativeApp/build_support.py'))['wait']()
command = ['nice', '-n', '10', 'xcrun', 'swiftc', '-num-threads', '2',
           root / 'App/NativeGameProfile.swift', root / 'App/PreparedAddOn.swift', root / 'App/SwitchPackage.swift',
           root / 'App/HomebrewItem.swift', root / 'App/PreparedImportCleanup.swift', out / 'main.swift', '-o', out / 'prepared-package-tests']
subprocess.run([str(x) for x in command], check=True)
subprocess.run([str(out / 'prepared-package-tests'), str(out)], check=True)
