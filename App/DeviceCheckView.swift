import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct DeviceCheckView: View {
    @State private var snapshot: DeviceCheck?
    @State private var includeTeam = false
    @State private var copied = false
    @State private var refreshing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            SatchelPanel {
                Toggle("Include signing team in copied report", isOn: $includeTeam)
                    .accessibilityIdentifier("device-check-include-team")
                    .onChange(of: includeTeam) { _, _ in copied = false }
                Text("Reports omit the signing team by default. Including it shares your developer account identifier. Clipboard copies stay on this device and expire after five minutes.")
                    .font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                HStack {
                    Button {
                        guard let snapshot else { return }
                        UIPasteboard.general.setItems([[UTType.utf8PlainText.identifier: snapshot.report(includeTeam: includeTeam)]],
                            options: [.localOnly: true, .expirationDate: Date().addingTimeInterval(300)])
                        copied = true
                    } label: {
                        SatchelButtonLabel(title: copied ? "Copied" : "Copy report", systemImage: "doc.on.doc", kind: .accent(SatchelTheme.tide), height: 42)
                    }.buttonStyle(PressableStyle()).disabled(snapshot == nil || refreshing)
                        .accessibilityIdentifier("device-check-copy")
                    Button { Task { await refresh() } } label: {
                        SatchelButtonLabel(title: "Refresh", systemImage: "arrow.clockwise", kind: .secondary, height: 42)
                    }.buttonStyle(PressableStyle()).disabled(refreshing)
                        .accessibilityIdentifier("device-check-refresh")
                }
#if PREVIEW_GAME
                DeviceCheckClipboardFixture()
#endif
            }
            if let snapshot {
                SatchelPanel(title: "Running app") {
                    ForEach(snapshot.rows) { row in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(row.title).font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                            Text(row.value).font(SatchelTheme.mono(12)).fixedSize(horizontal: false, vertical: true)
                                .accessibilityIdentifier("device-check-" + row.id)
                        }
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Signing team (on device only)").font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                        Text(snapshot.team ?? "Unknown").font(SatchelTheme.mono(12))
                            .privacySensitive().accessibilityIdentifier("device-check-team")
                    }
                    Text("Sampled: \(snapshot.sampledAt.formatted(date: .abbreviated, time: .standard))")
                        .font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim)
                }
                SatchelPanel(title: "What the measurements mean") {
                    Text(DeviceCheck.measurementNote).font(.system(size: 13, weight: .medium, design: .rounded))
                        .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
                }
            } else {
                ProgressView("Checking this process…")
            }
        }
        .task { await refresh() }
    }

    @MainActor private func refresh() async {
        guard !refreshing else { return }
        refreshing = true; copied = false
        snapshot = await Task.detached(priority: .utility) { DeviceCheck.read() }.value
        refreshing = false
    }
}
