import Foundation

struct HomebrewItem: Codable, Identifiable {
    let id: String
    let name: String
    let bytes: Int
    let textBytes: UInt32
    let roBytes: UInt32
    let dataBytes: UInt32
    let bssBytes: UInt32
    let importedAt: Date
    var package: SwitchPackageSummary? = nil
    /// Written before verified Prepare removes a local import. Old indexes decode without it.
    var preparedSource: PreparedImportSource? = nil
    var fileExtension: String { package?.format.rawValue ?? "nro" }
    var formatLabel: String { fileExtension.uppercased() }
}

