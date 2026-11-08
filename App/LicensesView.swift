import SwiftUI

/// The open source notices shipped inside the app (Settings > About > Licenses), read from
/// the bundle as they are, so every MIT and other notice stays word for word.
struct LicensesView: View {
    let close: () -> Void
    @State private var notices: [LicenseNotices.Notice]?
    @State private var selected: LicenseNotices.Notice?
    @State private var text = ""

    var body: some View {
        PanelChrome(closeTitle: "Close licenses", identifier: "close-licenses", close: close) {
            ScreenReader { cut, geometry in
                let compact = geometry.size.width - cut.leading - cut.trailing < 760
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        FoldSplit(fold: fold, cut: cut) {
                            list.frame(maxWidth: 300, alignment: .leading).frame(maxWidth: .infinity, alignment: .leading)
                        } second: {
                            reader(reserve: fold.isVertical ? reserve : 0)
                        }
                    } else {
                        HStack(alignment: .top, spacing: compact ? 14 : 24) {
                            list.frame(width: compact ? 196 : 240)
                            reader(reserve: reserve)
                        }
                        .cutoutPadding(cut, leading: compact ? 16 : 24, trailing: compact ? 16 : 24)
                    }
                }
            }
            .background { SatchelBackdrop(glow: SatchelTheme.violet) }
            .satchelMenuStatus()
        }
        .controllerNavigation(back: close)
        .task {
            let found = await Task.detached(priority: .userInitiated) { LicenseNotices.bundled() }.value
            notices = found
            if selected == nil { selected = found.first }
        }
        .task(id: selected) {
            guard let url = selected?.url else { text = ""; return }
            text = await Task.detached(priority: .userInitiated) { LicenseNotices.read(url) }.value
        }
    }

    private var list: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                BrandMark(size: 24)
                Text("Licenses").font(SatchelTheme.display(20))
            }
            .padding(.vertical, 18).padding(.leading, 6)
            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(notices ?? []) { notice in row(notice) }
                }
                .padding(.bottom, 18)
            }
            .scrollIndicators(.hidden)
            .accessibilityIdentifier("licenses-list")
        }
    }

    private func row(_ notice: LicenseNotices.Notice) -> some View {
        let chosen = notice == selected
        return Button { Haptics.tick(); selected = notice } label: {
            HStack(spacing: 10) {
                Image(systemName: "doc.text.fill").font(.system(size: 12, weight: .bold))
                    .foregroundStyle(chosen ? SatchelTheme.shell : SatchelTheme.violet)
                    .frame(width: 26, height: 26)
                    .background(chosen ? AnyShapeStyle(SatchelTheme.violet) : AnyShapeStyle(SatchelTheme.violet.opacity(0.15)),
                                in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                Text(notice.name).font(.system(size: 13, weight: .bold, design: .rounded))
                    .lineLimit(2).multilineTextAlignment(.leading)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
            }
            .foregroundStyle(SatchelTheme.text)
            .padding(.horizontal, 8).frame(minHeight: 44)
            .background(chosen ? SatchelTheme.surfaceHigh : .clear, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .contentShape(Rectangle())
        }
        .buttonStyle(PressableStyle(scale: 0.97, haptic: false))
        .accessibilityAddTraits(chosen ? .isSelected : [])
        .accessibilityIdentifier("license-" + notice.id)
        .controllerAction("license-" + notice.id, style: .row(.rounded(12))) { Haptics.tick(); selected = notice }
    }

    private func reader(reserve: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(selected?.name ?? "Licenses").font(SatchelTheme.display(22)).lineLimit(2)
                .padding(.trailing, reserve)
                .accessibilityIdentifier("licenses-title")
            if let notices, notices.isEmpty {
                SatchelPanel {
                    Text("No notices were found in this build.").font(.system(size: 14, weight: .medium, design: .rounded))
                        .foregroundStyle(SatchelTheme.textDim)
                }
            } else {
                ScrollView {
                    Text(verbatim: text).font(SatchelTheme.mono(11, .regular)).foregroundStyle(SatchelTheme.text)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(16)
                        .accessibilityIdentifier("licenses-text")
                }
                .scrollIndicators(.visible)
                .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: 22, style: .continuous).strokeBorder(SatchelTheme.line) }
                .accessibilityIdentifier("licenses-reader")
            }
        }
        .padding(.vertical, 18)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

/// Finds and reads the bundled notices, off the main thread.
enum LicenseNotices {
    struct Notice: Identifiable, Hashable, Sendable {
        let id: String
        let name: String
        let url: URL
    }

    /// A file is a notice when it sits in a Notices folder (the build's NativeAssets/Notices)
    /// or its name says so (App/Resources files land in the bundle root).
    static func isNotice(_ path: String) -> Bool {
        let parts = path.split(separator: "/")
        guard let file = parts.last, ["txt", "md"].contains((file as NSString).pathExtension.lowercased()) else { return false }
        if parts.dropLast().contains("Notices") { return true }
        let upper = file.uppercased()
        return ["LICENSE", "NOTICE", "COPYING", "PATENTS", "-MIT", ".MIT", "-BSD", ".BSD", "APACHE", "OFL"].contains { upper.contains($0) }
    }

    static func bundled(in root: URL? = Bundle.main.resourceURL) -> [Notice] {
        guard let root, let walk = FileManager.default.enumerator(at: root, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey],
                                                                  options: [.skipsHiddenFiles, .skipsPackageDescendants]) else { return [] }
        var seen = Set<String>(), result: [Notice] = []
        let base = root.standardizedFileURL.path
        for case let url as URL in walk {
            let path = String(url.standardizedFileURL.path.dropFirst(base.count + 1))
            // Code and frameworks carry their own notices inside; keep to the app's resources.
            if walk.level > 4 || path.hasSuffix(".framework") || path.hasSuffix(".bundle") { walk.skipDescendants(); continue }
            guard isNotice(path), let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
                  values.isRegularFile == true else { continue }
            let file = url.deletingPathExtension().lastPathComponent
            // The same notice can ship from two places; list it once.
            guard seen.insert(file.lowercased() + "#\(values.fileSize ?? 0)").inserted else { continue }
            let name = file.replacingOccurrences(of: "[-_.]", with: " ", options: .regularExpression)
            result.append(Notice(id: path.replacingOccurrences(of: "/", with: "-"), name: name, url: url))
        }
        return result.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    static func read(_ url: URL) -> String {
        guard let size = (try? url.resourceValues(forKeys: [.fileSizeKey]))?.fileSize, size <= 4 * 1024 * 1024,
              let data = try? Data(contentsOf: url) else { return "This notice could not be read." }
        return String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1) ?? "This notice could not be read."
    }
}
