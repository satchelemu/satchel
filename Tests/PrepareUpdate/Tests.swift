// Synthetic title IDs and versions only.
var failures = 0, checks = 0
func expect(_ condition: Bool, _ what: String) {
    checks += 1
    if !condition { failures += 1; print("FAIL: " + what) }
}
func game(_ id: UInt64, _ kind: GameInfo.Kind, _ version: UInt32, _ shown: String = "", bundles: Bool = false) -> GameInfo {
    var info = GameInfo(applicationID: id, kind: kind, version: version, displayVersion: shown, title: "Fixture", publisher: "",
                        hasIcon: false, accent: nil)
    info.bundlesUpdate = bundles
    return info
}
func caution(_ base: GameInfo, _ selected: GameInfo?, _ candidates: [(id: String, info: GameInfo)], id: UInt64? = nil) -> String? {
    PrepareUpdateCaution.text(base: base, applicationID: id ?? base.applicationID, selected: selected, among: candidates)
}

let base: UInt64 = 0x0100_ABCD_0001_0000, other: UInt64 = 0x0100_ABCD_0002_0000
let baseInfo = game(base, .application, 0, "1.0.0")
let update = (id: "update-141", info: game(base, .update, 0x60000, "1.4.1"))
let older = (id: "update-110", info: game(base, .update, 0x10000, "1.1.0"))
let ownID = (id: "update-own-id", info: game(base | 0x800, .update, 0x30000, "1.3.0"))
let foreign = (id: "other-update", info: game(other | 0x800, .update, 0x90000, "9.0.0"))
let extra = (id: "extra-update", info: game(base + 0x2000, .update, 0x90000, "9.0.0"))   // another program on the cartridge
let dlc = (id: "dlc", info: game(base, .addOnContent, 0x90000))
let all = [older, foreign, update, extra, dlc, ownID]

// No update chosen: the screen's existing wording, with both versions.
let missing = caution(baseInfo, nil, all)
expect(missing == "This game will be prepared without its imported update (base v1.0.0; update v1.4.1 is left out).", "base only (\(missing ?? "nil"))")
expect(caution(baseInfo, update.info, all) == nil, "the newest update chosen: no caution")
// An older update of the same game chosen while a newer one is imported.
let stale = caution(baseInfo, older.info, all)
expect(stale == "A newer imported update is not selected (v1.4.1; v1.1.0 is selected). Choose it under Update to prepare the game with it.",
       "older update chosen (\(stale ?? "nil"))")
expect(caution(baseInfo, ownID.info, [older, ownID]) == nil, "an update named by its own ID (base | 0x800) counts as chosen")
// Another game's update counts as none.
expect(caution(baseInfo, foreign.info, all)?.hasPrefix("This game will be prepared without its imported update") == true, "another game's update counts as none")
// Nothing matching: other games, other programs on the cartridge and DLC never match.
expect(caution(baseInfo, nil, [foreign, extra, dlc]) == nil, "nothing matching imported: no caution")
// A cartridge with its update bundled counts through its own version.
let bundled = game(base, .application, 0x60000, "1.4.1", bundles: true)
expect(caution(bundled, nil, all) == nil, "bundled update as new as any import: no caution")
expect(caution(game(base, .application, 0x10000, "1.1.0", bundles: true), nil, all)?.contains("update v1.4.1 is left out") == true,
       "bundled update older than an import: caution")
// The title ID typed for a multi-title dump is the target.
expect(caution(baseInfo, nil, all, id: other)?.contains("update v9.0.0 is left out") == true, "the typed title ID selects that game's update")
let unnamed = caution(game(base, .application, 0), nil, [(id: "u", info: game(base, .update, 0x10000))])
expect(unnamed == "This game will be prepared without its imported update.", "no display versions (\(unnamed ?? "nil"))")
for text in [missing, stale, unnamed].compactMap({ $0 }) {
    expect(!text.contains("\u{2014}") && !text.contains("--"), "no em dash or double hyphen: " + text)
}

print("prepare update caution: \(checks - failures) of \(checks) checks passed")
if failures > 0 { exit(1) }
