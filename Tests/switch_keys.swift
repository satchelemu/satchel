import Foundation

@main struct SwitchKeyTests {
    static func main() throws {
        let header = String(repeating: "13", count: 32)
        let area = String(repeating: "24", count: 16)
        let text = "# synthetic fixture only\nheader_key = \(header)\nkey_area_key_application_15 = \(area)\nignored_console_secret = not-retained\n"
        let keys = try SwitchKeys.parse(Data(text.utf8))
        precondition(keys.count == 2 && keys["header_key"]?.count == 32)
        precondition(keys["key_area_key_application_15"] == Data(repeating: 0x24, count: 16))
        let invalid = ["header_key = short", text + "header_key = \(header)\n",
                       text.replacingOccurrences(of: area, with: String(repeating: "+1", count: 16)),
                       text.replacingOccurrences(of: area, with: String(repeating: "zz", count: 16)),
                       "header_key = \(header)", String(repeating: "x", count: SwitchKeys.maximumFileSize + 1)]
        for fixture in invalid {
            do { _ = try SwitchKeys.parse(Data(fixture.utf8)); fatalError("Invalid key fixture accepted") }
            catch is SwitchKeyFailure { }
        }
        print("Console key parser: selection, lengths, duplicates, invalid hex, missing key-area material and size bounds passed. No Keychain changes made.")
    }
}
