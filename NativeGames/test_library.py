#!/usr/bin/env python3
"""Run the actual directory discovery code with synthetic package manifests on the Mac."""
from pathlib import Path
import argparse,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=Path.home()/'SatchelBuild/games/library-tests'

def main():
    global OUT
    parser=argparse.ArgumentParser()
    parser.add_argument('--out-root',type=Path,default=OUT)
    OUT=parser.parse_args().out_root
    OUT.mkdir(parents=True,exist_ok=True)
    packages=OUT/'PreparedGames';packages.mkdir(exist_ok=True)
    for name,title,version,signing in [('Mario','0100000000010000','1.4.1','signed'),('Zelda','0100F2C0115B6000','1.4.3',None),('Castlevania','0100BA4026E94000','1.0.0','signed'),('Unknown','010000000000FFFF','1.0.0','signed'),('Unsigned','0100000000010000','1.4.1','needs signature')]:
        p=packages/name;p.mkdir(exist_ok=True)
        manifest=dict(titleID=title,version=version)
        if signing is not None:manifest['signingStatus']=signing
        (p/'manifest.json').write_text(json.dumps(manifest))
    (packages/'Incomplete').mkdir(exist_ok=True)
    (packages/'.hidden').mkdir(exist_ok=True)
    source=(ROOT/'NativeHost/PreparedGame.swift').read_text()
    choice=source.split('private struct PreparedGameChoice:')[1].split('\nstruct PreparedGamesView:')[0]
    method=source.split('    private nonisolated static func discover()')[1].split('\n    }\n',1)[0]+'\n    }\n'
    method=method.replace('let root = fm.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames")','let root = URL(fileURLWithPath: CommandLine.arguments[1])')
    harness='import Foundation\nstruct PreparedGameChoice:'+choice+'\nfunc discover()'+method+'''
let recipe = NativePreparationRecipe(schema: 1, titleID: "010000000000ABCD", version: "1", contentVersion: 0,
    name: "Synthetic experimental", modules: ["main"], buildIDs: ["main": String(repeating: "1", count: 32)],
    sdkVersions: [], systemResource: 0, usesHeap: false, largeBSS: 0, usedMemory: 0x4000000, nros: [], reviewedVersion: nil)
let generated = try recipe.profile()
var native: [String: Any] = ["format": 2, "titleID": recipe.titleID, "version": recipe.version, "signingStatus": "signed",
    "recipe": try JSONSerialization.jsonObject(with: JSONEncoder().encode(recipe)),
    "preparedProfile": try JSONSerialization.jsonObject(with: JSONEncoder().encode(generated))]
for name in ["Experimental", "AlteredRecipe"] {
    let folder = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent(name)
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    if name == "AlteredRecipe" { var changed = native["preparedProfile"] as! [String: Any]; changed["environment"] = ["DM_SAVE": "/outside"]; native["preparedProfile"] = changed }
    try JSONSerialization.data(withJSONObject: native).write(to: folder.appendingPathComponent("manifest.json"))
}
let choices = try discover()
precondition(choices.count == 8)
let byID = Dictionary(uniqueKeysWithValues: choices.map { ($0.id, $0) })
precondition(byID["Mario"]?.supported == true)
precondition(byID["Zelda"]?.supported == true)
precondition(byID["Castlevania"]?.supported == true)
precondition(byID["Unknown"]?.supported == false && byID["Unknown"]?.detail == NativeGameProfile.unsupportedMessage)
precondition(byID["Unsigned"]?.supported == false && byID["Unsigned"]?.detail == "Prepared game needs signature.")
precondition(byID["Incomplete"]?.supported == false)
precondition(byID["Experimental"]?.supported == true && byID["Experimental"]?.name == "Synthetic experimental")
precondition(byID["AlteredRecipe"]?.supported == false)
print("PASS: library discovery accepts three reviewed games and a valid experimental recipe, and rejects four invalid package cases.")
'''
    (OUT/'main.swift').write_text(harness)
    shutil.copyfile(ROOT/'App/NativeGameProfiles.json',OUT/'NativeGameProfiles.json')
    exe=OUT/'test-library'
    subprocess.run(['nice','-n','10','xcrun','swiftc','-num-threads','2',ROOT/'App/NativeGameProfile.swift',OUT/'main.swift','-o',exe],check=True)
    subprocess.run([exe,packages],check=True)

if __name__=='__main__':main()
