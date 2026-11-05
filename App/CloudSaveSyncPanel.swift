import SwiftUI
import UniformTypeIdentifiers

/// Settings > Storage: one folder (usually in iCloud Drive) that every game's save syncs
/// through, or off. The engine keeps one packed file per game there and decides which way to
/// copy at each game start; Satchel's own save manager and backups stay on this iPhone.
struct CloudSaveSyncPanel: View {
    @State private var folder: String? = SatchelCloudSaveFolderName()
    @State private var needsChoosing = SatchelCloudSaveFolderNeedsChoosing()
    @State private var picking = false
    @State private var failure: String?

    var body: some View {
        SatchelPanel(title: "iCloud save sync", tint: SatchelTheme.tide) {
            HStack(spacing: 10) {
                Image(systemName: folder == nil ? "icloud.slash" : "icloud.fill")
                    .foregroundStyle(folder == nil ? SatchelTheme.textDim : SatchelTheme.tide)
                Text(folder.map { "On: \($0)" } ?? "Off")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                    .accessibilityIdentifier("cloud-saves-state")
                Spacer()
            }
            if needsChoosing && folder == nil {
                Text("The folder chosen earlier cannot be opened by this copy of Satchel (it was signed again). Choose it again to turn sync back on.")
                    .font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.spark)
                    .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 10) {
                Button { chooseFolder() } label: {
                    SatchelButtonLabel(title: folder == nil ? "Choose folder" : "Change folder", systemImage: "folder.badge.plus", kind: .secondary, height: 42)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("cloud-saves-choose")
                if folder != nil {
                    Button {
                        SatchelClearCloudSaveFolder(); reload()
                    } label: {
                        SatchelButtonLabel(title: "Turn off", systemImage: "icloud.slash", kind: .secondary, height: 42)
                    }
                    .buttonStyle(PressableStyle())
                    .accessibilityIdentifier("cloud-saves-off")
                }
            }
            Text("Each game's save is kept as one file in this folder and synced when the game starts, a few seconds after it saves, when Satchel goes to the background and when you quit. If both this iPhone and another device changed a save, the newer one is used and the other is kept beside the save on this iPhone. Export, import and automatic backups on the game's page work as before; an import takes effect in iCloud the next time you play. Changes apply from the next game you start.")
                .font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
            if let failure {
                Text(failure).font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.spark)
                    .accessibilityIdentifier("cloud-saves-error")
            }
        }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.folder], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { choose(url) }
            case .failure:
                failure = "The folder could not be opened."
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .SatchelCloudSaveFolderDidChange)) { _ in reload() }
    }

    private func chooseFolder() {
        failure = nil
        picking = true
    }

    private func choose(_ url: URL) {
        failure = SatchelChooseCloudSaveFolder(url) ? nil : "Satchel could not keep access to that folder. Choose a folder in iCloud Drive or On My iPhone."
        reload()
    }

    private func reload() {
        folder = SatchelCloudSaveFolderName()
        needsChoosing = SatchelCloudSaveFolderNeedsChoosing()
    }
}
