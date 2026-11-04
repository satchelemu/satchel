import SwiftUI

struct SignatureExpiryBanner: View {
    let warnings: [SignatureExpiry.Warning]
    let now: Date
    @State private var expanded = false

    var body: some View {
        if let first = warnings.first {
            VStack(alignment: .leading, spacing: 8) {
                Label(first.message(now: now), systemImage: "clock.badge.exclamationmark")
                    .fixedSize(horizontal: false, vertical: true)
                if warnings.count > 1 {
                    Button(expanded ? "Show less" : "\(warnings.count - 1) more signature reminders") { expanded.toggle() }
                    if expanded {
                        ScrollView {
                            VStack(alignment: .leading, spacing: 8) {
                                ForEach(warnings.dropFirst()) { warning in Text(warning.message(now: now)) }
                            }
                        }.frame(maxHeight: 100)
                    }
                }
            }
            .font(.system(size: 12, weight: .medium, design: .rounded))
            .foregroundStyle(SatchelTheme.spark).padding(12)
            .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 16))
            .accessibilityIdentifier("signature-expiry-warning")
        }
    }
}

#if !SATCHEL_UI_PREVIEW_MINIMAL && !PREVIEW_GAME
extension SignatureExpiry {
    static func readWarnings(games: [(folder: String, name: String)], now: Date) -> [Warning] {
        let app = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision")
            .flatMap { try? boundedData($0) }
            .flatMap { SatchelSignatureMetadata.profileContent($0) }
            .flatMap(profileExpiry)
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PreparedGames", isDirectory: true)
        let dates: [(String, String, Date)] = games.compactMap { game in
            guard !game.folder.isEmpty, !game.folder.contains("/"), game.folder != ".", game.folder != "..",
                  let cms = try? gameCMS(root.appendingPathComponent(game.folder).appendingPathComponent("game.dylib")),
                  let date = SatchelSignatureMetadata.signerExpiry(cms) else { return nil }
            return (game.folder, game.name, date)
        }
        // Missing profiles and old or unsigned packages have unknown dates, never invented ones.
        return warnings(app: app, games: dates, now: now)
    }
}
#endif
