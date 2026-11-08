import SwiftUI

struct SignatureExpiryBanner: View {
    let warnings: [SignatureExpiry.Warning]
    let now: Date
    @State private var expanded = false
    @State private var dismissed = SignatureExpiryBanner.dismissedThisLaunch

    /// Reminders dismissed since launch. They come back on the next launch, and a reminder
    /// with a new date (a refresh that did not reach far enough) shows again at once.
    private static var dismissedThisLaunch: Set<String> = []
    static func key(_ warning: SignatureExpiry.Warning) -> String { warning.id + "@\(warning.expiry.timeIntervalSince1970)" }
    private var visible: [SignatureExpiry.Warning] { warnings.filter { !dismissed.contains(Self.key($0)) } }

    var body: some View {
        let shown = visible
        if let first = shown.first {
            let tint = first.expiry <= now ? SatchelTheme.ember : SatchelTheme.spark
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: "clock.badge.exclamationmark").font(.system(size: 14, weight: .bold)).foregroundStyle(tint)
                    .frame(width: 30, height: 30).background(tint.opacity(0.16), in: Circle())
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 6) {
                    Text(first.message(now: now)).font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(SatchelTheme.text).fixedSize(horizontal: false, vertical: true)
                    if shown.count > 1 {
                        Button {
                            Haptics.tick()
                            withAnimation(SatchelTheme.snap) { expanded.toggle() }
                        } label: {
                            Label(expanded ? "Show less" : "\(shown.count - 1) more signature reminders",
                                  systemImage: expanded ? "chevron.up" : "chevron.down")
                                .font(.system(size: 12, weight: .bold, design: .rounded)).foregroundStyle(tint)
                        }
                        .buttonStyle(PressableStyle(scale: 0.97, haptic: false))
                        .controllerAction("signature-expiry-more", style: .button(.capsule, inset: -6)) { expanded.toggle() }
                        if expanded {
                            ScrollView {
                                VStack(alignment: .leading, spacing: 8) {
                                    ForEach(shown.dropFirst()) { warning in
                                        Text(warning.message(now: now)).fixedSize(horizontal: false, vertical: true)
                                    }
                                }
                            }
                            .frame(maxHeight: 100)
                            .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                        }
                    }
                }
                Spacer(minLength: 0)
                Button { dismiss(shown) } label: {
                    Image(systemName: "xmark").font(.system(size: 12, weight: .bold)).foregroundStyle(SatchelTheme.textDim)
                        .frame(width: 30, height: 30).background(Color.white.opacity(0.07), in: Circle())
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .buttonStyle(PressableStyle(scale: 0.9))
                .accessibilityLabel("Dismiss until next launch")
                .accessibilityIdentifier("signature-expiry-dismiss")
                .controllerAction("signature-expiry-dismiss", style: .button(.circle, inset: 7)) { dismiss(shown) }
            }
            .padding(.leading, 10).padding(.vertical, 4).padding(.trailing, 2)
            .background(SatchelTheme.surface.opacity(0.94), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(tint.opacity(0.45)) }
            .transition(.opacity.combined(with: .move(edge: .bottom)))
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("signature-expiry-warning")
        }
    }

    private func dismiss(_ shown: [SignatureExpiry.Warning]) {
        Haptics.tick()
        let keys = Set(shown.map(Self.key))
        Self.dismissedThisLaunch.formUnion(keys)
        withAnimation(SatchelTheme.glide) { dismissed.formUnion(keys) }
    }
}

#if !SATCHEL_UI_PREVIEW_MINIMAL && !PREVIEW_GAME
extension SignatureExpiry {
    static func readWarnings(games: [(folder: String, name: String)], now: Date) -> [Warning] {
        let profile = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision")
            .flatMap { try? boundedData($0) }
            .flatMap { SatchelSignatureMetadata.profileContent($0) }
            .flatMap(profileExpiry)
        let signer = Bundle.main.executableURL
            .flatMap { try? codeCMS($0, executable: true) }
            .flatMap { SatchelSignatureMetadata.signerExpiry($0) }
        let app = appExpiry(profile: profile, signer: signer)
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
