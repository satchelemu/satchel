import SwiftUI
import UIKit

@MainActor final class AddOnJob: ObservableObject {
    static let shared = AddOnJob()
    @Published private(set) var running = false
    @Published private(set) var package = ""
    @Published private(set) var message = ""
    @Published private(set) var cancelling = false
    private var token: PrepareCancellation?
    private var artwork: GameArtworkStore?
    private var background: UIBackgroundTaskIdentifier = .invalid

    func cancel() {
        guard token?.cancel() == true else { return }
        cancelling = true; message = "Cancelling. Your prepared game will be kept."
    }

    func start(package: String, sources: [URL], artwork: GameArtworkStore) {
        guard !running else { return }
        self.package = package
        guard !PrepareJob.shared.running, !WarmupJob.shared.running, !SatchelNativeSessionStarted(), PreparedLaunch.current == nil else {
            message = "Quit the game and wait for preparation or warm-up to finish before adding DLC."; return
        }
        guard !package.isEmpty, !package.contains("/"), package != ".", package != ".." else { return }
        do {
            guard let keys = try SwitchKeys.load() else { throw PrepareFailure(message: FirstRunHelp.noKeys) }
            let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("PreparedGames/" + package)
            let token = PrepareCancellation(); self.token = token
            self.artwork = artwork
            self.package = package; running = true; cancelling = false; message = "Checking the prepared game…"
            UIApplication.shared.isIdleTimerDisabled = true
            background = UIApplication.shared.beginBackgroundTask(withName: "Add DLC") { token.cancel() }
            DispatchQueue.global(qos: .utility).async {
                let result = Result {
                    try AddOnUpdate.run(root: root, sources: sources, keys: keys, token: token) { phase in
                        let text: String
                        switch phase {
                        case .checking: text = "Checking the prepared game…"
                        case .cloning: text = "Staging a safe copy…"
                        case .extracting: text = "Reading and verifying DLC…"
                        case .binding: text = "Updating DLC records…"
                        case .verifying: text = "Verifying the complete staged package…"
                        case .publishing: text = "Finishing Add DLC…"
                        }
                        DispatchQueue.main.async { if !self.cancelling { self.message = text } }
                    }
                }
                DispatchQueue.main.async {
                    switch result {
                    case .success(let count): self.message = count == 0 ? "This DLC is already included." : "Added \(count) DLC. Ready to play."
                    case .failure(let error) where error is CancellationError:
                        self.message = "Add DLC cancelled. Your prepared game and imports were kept."
                    case .failure(let error): self.message = "DLC was not added. Your prepared game was kept. " + error.localizedDescription
                    }
                    self.running = false; self.token = nil; self.cancelling = false
                    UIApplication.shared.isIdleTimerDisabled = false
                    if self.background != .invalid { UIApplication.shared.endBackgroundTask(self.background); self.background = .invalid }
                    self.artwork?.refreshPrepared(); self.artwork = nil
                }
            }
        } catch { message = FirstRunHelp.message(error) }
    }
}
