#!/usr/bin/env python3
"""Exercise the app's actual import queue and index with synthetic splits on macOS."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ['SATCHEL_OUT']) / 'split-library'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'App/HomebrewLibrary.swift').read_text()
# Retain the entire import path verbatim. Omit unrelated execution/deletion UI;
# replace only the sandbox Documents location, so this cannot touch the real library.
source = source.split('    /// Developer hook so the Mac can tidy')[0] + '}\n'
source = source.replace('FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]',
                        'URL(fileURLWithPath: CommandLine.arguments[1])')
(OUT / 'Library.swift').write_text('import Combine\n' + source)
(OUT / 'Bridge.h').write_text(f'#include "{ROOT}/Core/HomebrewFormat.h"\n#include "{ROOT}/Core/NROLoader.h"\n')
(OUT / 'Main.swift').write_text('''import Foundation
// Preparation cleanup is outside this import-only harness.
struct PreparedImportSource: Codable {}
@main struct LibraryCheck {
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        func fixture(_ name: String, value: UInt8) throws -> [URL] {
            let folder = root.appendingPathComponent(name)
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            var bytes = Data("PFS0".utf8)
            func put(_ n: UInt64, _ width: Int) {
                for i in 0..<width { bytes.append(UInt8(truncatingIfNeeded: n >> (8 * i))) }
            }
            let payload = Data(repeating: value, count: 1048576)
            put(1, 4); put(6, 4); put(0, 4); put(0, 8); put(UInt64(payload.count), 8); put(0, 4); put(0, 4)
            bytes.append(Data("a.nca\\0".utf8)); bytes.append(payload)
            let parts = [folder.appendingPathComponent("00"), folder.appendingPathComponent("01")]
            try bytes.prefix(3).write(to: parts[0]); try bytes.dropFirst(3).write(to: parts[1])
            return parts
        }
        func finish(_ library: HomebrewLibrary) {
            let deadline = Date().addingTimeInterval(90)
            while library.importing && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            precondition(!library.importing && library.queuedImports == 0)
        }
        let a = try fixture("First game", value: 1), b = try fixture("Second game", value: 2)
        let library = HomebrewLibrary()
        library.importFiles([a[1], b[0], a[0], b[1]])
        finish(library)
        precondition(library.items.count == 2 && Set(library.items.map { $0.name }) == ["First game", "Second game"])
        let ids = Set(library.items.map { $0.id })
        library.importFiles([a[0].deletingLastPathComponent()]); finish(library)
        precondition(Set(library.items.map { $0.id }) == ids)
        guard case .added(_, let duplicate) = library.lastOutcome else { fatalError("Missing duplicate result") }
        precondition(duplicate)
        library.importFiles([b[1]]); finish(library)
        guard case .failed = library.lastOutcome else { fatalError("Missing part accepted") }
        precondition(library.items.count == 2)
        let reopened = HomebrewLibrary()
        precondition(Set(reopened.items.map { $0.id }) == ids)
        let files = try FileManager.default.contentsOfDirectory(atPath: root.appendingPathComponent("Homebrew").path)
        precondition(files.count == 3 && files.filter { $0.hasSuffix(".nsp") }.count == 2)
        print("PASS: actual library import queue groups interleaved parts into two games, folder reimport deduplicates, missing first part fails, persisted index reopens, no staging leftovers")
    }
}
''')
for name in ['HomebrewFormat', 'NROLoader']:
    subprocess.run(['xcrun', 'clang', '-c', ROOT / ('Core/' + name + '.c'), '-o', OUT / (name + '.o')], check=True)
subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', '-j', '2', '-num-threads', '2', '-import-objc-header', OUT / 'Bridge.h',
                ROOT / 'App/SwitchPackage.swift', ROOT / 'App/SwitchPackageImport.swift', ROOT / 'App/HomebrewItem.swift', ROOT / 'App/FirstRunHelp.swift',
                OUT / 'Library.swift', OUT / 'Main.swift', OUT / 'HomebrewFormat.o', OUT / 'NROLoader.o', '-o', OUT / 'library-check'], check=True)
subprocess.run([OUT / 'library-check', OUT / 'synthetic-documents'], check=True)
