#!/usr/bin/env python3
"""Build Mac tests using the production metadata reader, packager and manifest checks."""
import os
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
for name in ['executable','romfs','content','package_reader']:
    (out/(name+'_fixtures.swift')).write_text((root/('Tests/'+('package_reader' if name == 'package_reader' else 'switch_'+name)+'.swift')).read_text().replace('@main',''))
source=(root/'App/GameArtwork.swift').read_text().split('/// A package prepared for native play')[0]
source=source.replace('import UIKit','import AppKit\ntypealias UIImage = NSImage\nextension NSImage { var cgImage: CGImage? { cgImage(forProposedRect: nil, context: nil, hints: nil) } }\nextension UInt64 { var titleIDString: String { String(format: "%016llX", self) } }')
(out/'GameInfoReader.swift').write_text(source)
shelf=(root/'App/GameShelf.swift').read_text()
entry=shelf[shelf.index('struct ShelfEntry:'):shelf.index('/// Where a library entry')]
method=shelf[shelf.index('    func shelf('):shelf.index('    /// "Publisher')]
(out/'Shelf.swift').write_text('import Foundation\n'+entry+"""
struct PreparedImportSource: Codable { let titleID: UInt64; let isUpdate: Bool }
struct ShelfPrepared { let applicationID: UInt64 }
final class GameArtworkStore {
    var infos: [String: GameInfo] = [:]
    func info(_ item: HomebrewItem) -> GameInfo? { infos[item.id] }
    func preparedPackage(for item: HomebrewItem) -> ShelfPrepared? { nil }
"""+method+'}\n')
(out/'NativeGameProfiles.json').write_bytes((root/'App/NativeGameProfiles.json').read_bytes())
(out/'Bridge.h').write_text(''.join('#include "'+str(root/'Core'/n)+'"\n' for n in ['SwitchCrypto.h','SwitchStorage.h','SwitchCompression.h','NSOLoader.h']))
def run(a): subprocess.run([str(x) for x in a],check=True,cwd=root)
for c,o in [('SwitchCrypto.c','crypto.o'),('NSOLoader.c','nso.o')]:
    run(['xcrun','clang','-O1','-Wall','-Wextra','-Werror','-c',root/'Core'/c,'-o',out/o])
verifier=(root/'NativeHost/PreparedGame.swift').read_text().split('\n/// The background time around the package check')[0].split('\nstruct PreparedGameView: View {')[0].replace('import SwiftUI','import Foundation')
for scope in ['fm','FileManager.default']:
    for directory in ['documentDirectory','applicationSupportDirectory']:
        verifier=verifier.replace(f'{scope}.urls(for: .{directory}, in: .userDomainMask)[0]', 'URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent("verification")')
(out/'Cancellation.swift').write_text((root/'NativePrepare/Rewrite.swift').read_text().split('private final class RewriteProgressContext')[0])
(out/'PreparedVerifier.swift').write_text(verifier + '\nfunc SatchelNativeSessionStarted() -> Bool { false }\n')
sources=['SwitchPackage','SwitchPackageImport','SwitchControl','SwitchExecutable','SwitchRomFS','SwitchStorage','SwitchKeys','SwitchContentMeta','SwitchPatchStorage','SwitchContentPipeline','SwitchCompressedInput','SwitchRomFSImage','SwitchDecodedModule','SwitchAddOnContent','PreparedAddOn','NativeGameProfile','HomebrewItem']
run(['xcrun','swiftc','-swift-version','5','-O','-num-threads','2','-import-objc-header',out/'Bridge.h',*[root/'App'/(s+'.swift') for s in sources],*[out/(s+'_fixtures.swift') for s in ['executable','romfs','content','package_reader']],out/'GameInfoReader.swift',out/'Shelf.swift',out/'PreparedVerifier.swift',out/'Cancellation.swift',root/'NativePrepare/AddOnUpdate.swift',root/'Tests/AddOnContent/UpdateTests.swift',root/'Tests/AddOnContent/Tests.swift',out/'crypto.o',out/'nso.o',Path.home()/'SatchelBuild/m5/archive-macosx/libShaderArchive.a','-o',out/'dlc-tests'])
run([out/'dlc-tests',out/'synthetic',*sys.argv[2:]])
