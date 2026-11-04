import CryptoKit
import SwiftUI
import UIKit

private final class HomebrewPlayerControls: @unchecked Sendable {
    private let lock = NSLock()
    private var buttons: UInt64 = 0
    private var pendingPresses: UInt64 = 0
    private var clearGeneration: UInt64 = 0
    private var paused = false
    private var cancelled = false

    func read() -> (buttons: UInt64, presses: UInt64, clearGeneration: UInt64, paused: Bool, cancelled: Bool) {
        lock.lock(); defer { lock.unlock() }
        let presses = pendingPresses
        pendingPresses = 0
        return (buttons, presses, clearGeneration, paused, cancelled)
    }
    func setButtons(_ value: UInt64) {
        lock.lock()
        pendingPresses |= value & ~buttons
        buttons = value
        lock.unlock()
    }
    func setPaused(_ value: Bool) {
        lock.lock()
        paused = value; buttons = 0; pendingPresses = 0; clearGeneration &+= 1
        lock.unlock()
    }
    func stop() {
        lock.lock()
        cancelled = true; buttons = 0; pendingPresses = 0; clearGeneration &+= 1
        lock.unlock()
    }
}

private struct HomebrewPlayerSnapshot {
    var image: CGImage?
    var frames: UInt64 = 0
    var frameChecksum: UInt64 = 0
    var instructions: UInt64 = 0
    var lastGameFrameSeconds = 0.0
    var activeInterpreterSeconds = 0.0
    var pc = "n/a"
    var status = "Starting homebrew…"
    var output = ""
    var finished = false
    var fault = false
}

/* Keep at most one pending image while the main thread is busy. Each CGImage
 * owns a copied Data buffer, never memory belonging to a running/destroyed VM.
 */
private final class HomebrewPlayerMailbox: @unchecked Sendable {
    private let lock = NSLock()
    private var pending: HomebrewPlayerSnapshot?
    private var scheduled = false
    func put(_ snapshot: HomebrewPlayerSnapshot) -> Bool {
        lock.lock(); defer { lock.unlock() }
        var next = snapshot
        if next.image == nil { next.image = pending?.image }
        pending = next
        if scheduled { return false }
        scheduled = true
        return true
    }
    func take() -> HomebrewPlayerSnapshot? {
        lock.lock(); defer { lock.unlock() }
        let snapshot = pending
        pending = nil; scheduled = false
        return snapshot
    }
}

private struct HomebrewPlayerFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

private final class HomebrewPlayerSession: ObservableObject {
    @Published private(set) var image: CGImage?
    @Published private(set) var frames: UInt64 = 0
    @Published private(set) var frameChecksum: UInt64 = 0
    @Published private(set) var instructions: UInt64 = 0
    @Published private(set) var lastGameFrameSeconds = 0.0
    @Published private(set) var activeInterpreterSeconds = 0.0
    @Published private(set) var pc = "n/a"
    @Published private(set) var status = "Starting homebrew…"
    @Published private(set) var output = ""
    @Published private(set) var paused = false
    @Published private(set) var finished = false
    @Published private(set) var fault = false
    @Published private(set) var stopping = false
    @Published private(set) var heldButtons: UInt64 = 0
    @Published private(set) var reportURL: URL?
    @Published private(set) var reportError: String?
    private let url: URL
    private let name: String
    private let expectedSHA256: String
    private let queue = DispatchQueue(label: "com.dell.satchel.player", qos: .userInitiated)
    private let controls = HomebrewPlayerControls()
    private let mailbox = HomebrewPlayerMailbox()
    private var started = false
    private var pulseGeneration: UInt64 = 0
    private var buttonGenerations: [UInt64: UInt64] = [:]

    init(url: URL, name: String, expectedSHA256: String) {
        self.url = url; self.name = name; self.expectedSHA256 = expectedSHA256
    }

    deinit { controls.stop() }

    func start() {
        guard !started else { return }
        started = true
        let controls = controls, mailbox = mailbox
        let url = url, expectedSHA256 = expectedSHA256
        queue.async { [weak self] in
            Self.execute(url: url, expectedSHA256: expectedSHA256, controls: controls) { [weak self] snapshot in
                guard mailbox.put(snapshot) else { return }
                DispatchQueue.main.async { [weak self] in
                    if let latest = mailbox.take() { self?.apply(latest) }
                }
            }
        }
    }

    func setButton(_ bit: UInt64, pressed: Bool) {
        guard !finished, !paused, !stopping else { return }
        pulseGeneration &+= 1
        buttonGenerations[bit] = pulseGeneration
        if pressed { heldButtons |= bit } else { heldButtons &= ~bit }
        controls.setButtons(heldButtons)
    }

    func accessiblePress(_ bit: UInt64) {
        setButton(bit, pressed: true)
        let generation = pulseGeneration
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self] in
            guard let self, generation == self.buttonGenerations[bit] else { return }
            self.setButton(bit, pressed: false)
        }
    }

    func setPaused(_ value: Bool) {
        guard !finished, !stopping else { return }
        paused = value; heldButtons = 0; pulseGeneration &+= 1
        buttonGenerations.removeAll()
        controls.setPaused(value)
    }

    func stop() {
        controls.stop(); heldButtons = 0; pulseGeneration &+= 1
        buttonGenerations.removeAll()
        if !finished { stopping = true; paused = false; status = "Stopping…" }
    }

    func saveReport() {
        let report: [String: Any] = [
            "name": name, "sha256": expectedSHA256,
            "timestamp": ISO8601DateFormatter().string(from: Date()),
            "status": paused ? "Paused" : status, "frames": frames,
            "frameChecksum": String(format: "%016llx", frameChecksum),
            "instructions": instructions, "pc": pc, "debugOutput": output,
            "lastGameFrameSeconds": lastGameFrameSeconds,
            "activeInterpreterSeconds": activeInterpreterSeconds,
            "timing": "Last complete interval between guest presentations, including copying/pacing; pause-crossing intervals skipped. Zero means no interval measured yet. Interpreter total excludes host waits and pauses.",
            "finished": finished, "fault": fault,
            "execution": "Interpreted NRO; displayed pixels copied from guest framebuffer"
        ]
        do {
            let destination = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("Homebrew-player-run.json")
            let data = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: destination, options: .atomic)
            reportURL = destination; reportError = nil
        } catch { reportError = "Could not save the run report: \(error.localizedDescription)" }
    }

    private func apply(_ snapshot: HomebrewPlayerSnapshot) {
        if let image = snapshot.image { self.image = image }
        frames = snapshot.frames; frameChecksum = snapshot.frameChecksum
        instructions = snapshot.instructions; pc = snapshot.pc
        lastGameFrameSeconds = snapshot.lastGameFrameSeconds
        activeInterpreterSeconds = snapshot.activeInterpreterSeconds
        output = snapshot.output; finished = snapshot.finished; fault = snapshot.fault
        if !stopping || snapshot.finished { status = snapshot.status }
        if snapshot.finished {
            heldButtons = 0; paused = false
            controls.setButtons(0)
            saveReport()
        }
    }

    private static func execute(url: URL, expectedSHA256: String, controls: HomebrewPlayerControls,
                                publish: (HomebrewPlayerSnapshot) -> Void) {
        var snapshot = HomebrewPlayerSnapshot()
        do {
            let file = try FileHandle(forReadingFrom: url)
            defer { try? file.close() }
            let size = try file.seekToEnd()
            guard size <= UInt64(SATCHEL_NRO_FILE_LIMIT) else {
                throw HomebrewPlayerFailure(message: "The local NRO exceeds the 64 MiB file limit.")
            }
            try file.seek(toOffset: 0)
            let data = try file.read(upToCount: Int(SATCHEL_NRO_FILE_LIMIT) + 1) ?? Data()
            guard data.count <= Int(SATCHEL_NRO_FILE_LIMIT) else {
                throw HomebrewPlayerFailure(message: "The local NRO exceeds the 64 MiB file limit.")
            }
            let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            guard digest == expectedSHA256 else {
                throw HomebrewPlayerFailure(message: "The local NRO changed since import. Import the original again to repair its local copy.")
            }
            var error = [CChar](repeating: 0, count: 256)
            let vm = data.withUnsafeBytes { buffer in
                satchel_create_nro(buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count, &error, error.count)
            }
            guard let vm else { throw HomebrewPlayerFailure(message: String(cString: error)) }
            defer { satchel_destroy(vm) }
            var lastFrameInstructions: UInt64 = 0
            var noFrameSeconds = 0.0
            var nextFrameTime = ProcessInfo.processInfo.systemUptime
            var lastProgressTime = nextFrameTime
            var lastClearGeneration: UInt64 = 0
            var lastPresentationTime: TimeInterval?
            while true {
                let input = controls.read()
                // Process even a pause/resume pair that happened between slices;
                // stale latched edges must never reach the guest after resuming.
                if input.clearGeneration != lastClearGeneration {
                    satchel_clear_buttons(vm)
                    lastClearGeneration = input.clearGeneration
                    lastPresentationTime = nil
                }
                if input.cancelled { snapshot.status = "Stopped by you"; break }
                if input.paused {
                    Thread.sleep(forTimeInterval: 0.01)
                    nextFrameTime = ProcessInfo.processInfo.systemUptime
                    continue
                }
                // Retain taps shorter than one interpreter slice. The core
                // latches these edges until the game's next input poll, while
                // the second call restores the current physical held state.
                if input.presses != 0 { satchel_set_buttons(vm, input.buttons | input.presses) }
                satchel_set_buttons(vm, input.buttons)
                let sliceStart = ProcessInfo.processInfo.systemUptime
                let result = satchel_frame(vm, 0, 0, 250_000)
                let sliceSeconds = ProcessInfo.processInfo.systemUptime - sliceStart
                snapshot.activeInterpreterSeconds += sliceSeconds
                noFrameSeconds += sliceSeconds
                if result == SATCHEL_WAIT { Thread.sleep(forTimeInterval: 0.001) }
                let now = ProcessInfo.processInfo.systemUptime
                snapshot.instructions = satchel_instructions(vm)
                snapshot.pc = String(format: "0x%llx", satchel_pc(vm))
                let frames = satchel_presented_frames(vm)
                if frames > snapshot.frames {
                    let width = Int(satchel_display_width(vm)), height = Int(satchel_display_height(vm))
                    guard width > 0, height > 0, width <= 4096, height <= 4096,
                          width * height <= 4096 * 2160, let pixels = satchel_display_pixels(vm) else {
                        throw HomebrewPlayerFailure(message: "The guest presented an invalid framebuffer size or address.")
                    }
                    let image = try autoreleasepool { () throws -> CGImage in
                        let pixelsCopy = Data(bytes: pixels, count: width * height * 4)
                        guard let provider = CGDataProvider(data: pixelsCopy as CFData),
                              let image = CGImage(width: width, height: height, bitsPerComponent: 8,
                                                  bitsPerPixel: 32, bytesPerRow: width * 4,
                                                  space: CGColorSpaceCreateDeviceRGB(),
                                                  // The emulated display layer is opaque; its RGBA
                                                  // byte buffer can contain unused/zero alpha bytes.
                                                  bitmapInfo: CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue)),
                                                  provider: provider, decode: nil, shouldInterpolate: false,
                                                  intent: .defaultIntent) else {
                            throw HomebrewPlayerFailure(message: "Could not display the guest's framebuffer.")
                        }
                        return image
                    }
                    snapshot.image = image; snapshot.frames = frames; snapshot.status = "Running"
                    snapshot.frameChecksum = satchel_display_checksum(vm)
                    snapshot.output = String(cString: satchel_output(vm))
                    let presentedAt = ProcessInfo.processInfo.systemUptime
                    if let previous = lastPresentationTime {
                        snapshot.lastGameFrameSeconds = presentedAt - previous
                    }
                    lastPresentationTime = presentedAt
                    publish(snapshot); snapshot.image = nil
                    noFrameSeconds = 0; lastFrameInstructions = snapshot.instructions
                    // Back-pressure guest presentation to 60 Hz, without accumulating
                    // a backlog if interpreting one frame takes longer than 1/60 s.
                    nextFrameTime = max(nextFrameTime + 1.0 / 60.0, now)
                    let delay = nextFrameTime - ProcessInfo.processInfo.systemUptime
                    if delay > 0 { Thread.sleep(forTimeInterval: min(delay, 1.0 / 60.0)) }
                    lastProgressTime = now
                } else if now - lastProgressTime >= 0.25 {
                    snapshot.output = String(cString: satchel_output(vm))
                    publish(snapshot); lastProgressTime = now
                }
                if result == SATCHEL_FAULT {
                    snapshot.status = String(cString: satchel_error(vm)); snapshot.fault = true; break
                }
                if result == SATCHEL_EXITED {
                    let code = satchel_exit_code(vm)
                    snapshot.status = code == 0 ? "Completed" : "Exited with code \(code)"
                    break
                }
                if noFrameSeconds >= 120 || snapshot.instructions - lastFrameInstructions >= 2_000_000_000 {
                    snapshot.status = "Stopped after 120 interpreter seconds or 2 billion instructions without a new display frame."
                    snapshot.fault = true; break
                }
            }
            snapshot.output = String(cString: satchel_output(vm))
        } catch {
            snapshot.status = error.localizedDescription; snapshot.fault = true
        }
        snapshot.finished = true
        publish(snapshot)
    }
}

/* UIControl touch events preserve press/release and simultaneous button holds.
 * UIKit also reports cancellation when a system gesture interrupts the touch.
 */
private final class HomebrewTouchButton: UIButton {
    var accessiblePress: (() -> Void)?
    override func accessibilityActivate() -> Bool { accessiblePress?(); return true }
}

private struct HomebrewHoldButton: UIViewRepresentable {
    let title: String
    let label: String
    let identifier: String
    let held: Bool
    let enabled: Bool
    let changed: (Bool) -> Void
    let accessiblePress: () -> Void

    final class Coordinator: NSObject {
        var changed: (Bool) -> Void
        init(changed: @escaping (Bool) -> Void) { self.changed = changed }
        @objc func down() { changed(true) }
        @objc func up() { changed(false) }
    }
    func makeCoordinator() -> Coordinator { Coordinator(changed: changed) }
    func makeUIView(context: Context) -> HomebrewTouchButton {
        let button = HomebrewTouchButton(type: .custom)
        button.titleLabel?.font = .systemFont(ofSize: 20, weight: .bold)
        button.layer.cornerRadius = 12
        button.addTarget(context.coordinator, action: #selector(Coordinator.down), for: [.touchDown, .touchDragEnter])
        button.addTarget(context.coordinator, action: #selector(Coordinator.up), for: [.touchUpInside, .touchUpOutside, .touchCancel, .touchDragExit])
        return button
    }
    func updateUIView(_ button: HomebrewTouchButton, context: Context) {
        context.coordinator.changed = changed
        button.setTitle(title, for: .normal)
        button.setTitleColor(held ? .black : .white, for: .normal)
        button.backgroundColor = held ? .systemMint : UIColor.black.withAlphaComponent(0.58)
        button.layer.borderWidth = 1
        button.layer.borderColor = UIColor.white.withAlphaComponent(0.22).cgColor
        button.isEnabled = enabled; button.alpha = enabled ? 1 : 0.4
        button.accessibilityLabel = label; button.accessibilityIdentifier = identifier
        button.accessiblePress = accessiblePress
    }
}

struct HomebrewPlayer: View {
    @StateObject private var session: HomebrewPlayerSession
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    @State private var showingDiagnostics = false
    @State private var landscapeStarted = false
    private let name: String

    init(url: URL, name: String, expectedSHA256: String) {
        self.name = name
        _session = StateObject(wrappedValue: HomebrewPlayerSession(url: url, name: name, expectedSHA256: expectedSHA256))
    }

    var body: some View {
        GeometryReader { geometry in
            let landscape = geometry.size.width > geometry.size.height
            ZStack {
                Color.black
                if landscape {
                    gameDisplay.frame(width: geometry.size.width, height: geometry.size.height)
                    controlsOverlay(in: geometry)
                } else {
                    VStack(spacing: 16) {
                        Image(systemName: "rectangle.landscape.rotate").font(.largeTitle)
                        Text("Waiting for the landscape display…")
                            .multilineTextAlignment(.center)
                        Button { close() } label: { SatchelRailLabel("Done", systemImage: "xmark") }
                    .accessibilityIdentifier("game-done")
                    }.padding(30).foregroundStyle(.white)
                }
            }
            .onChange(of: landscape, initial: true) { _, isLandscape in
                if isLandscape && !landscapeStarted {
                    landscapeStarted = true
                    session.start()
                } else if !isLandscape && landscapeStarted {
                    session.setPaused(true)
                }
            }
        }
        .ignoresSafeArea()
        .statusBarHidden(true)
        .onDisappear { session.stop() }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active { session.setPaused(true) }
        }
        .sheet(isPresented: $showingDiagnostics) { diagnostics }
    }

    private var gameDisplay: some View {
        ZStack {
            if let image = session.image {
                Image(image, scale: 1, label: Text("Guest game display"))
                    .resizable().interpolation(.none).scaledToFit()
                    .accessibilityIdentifier("game-display")
                    .accessibilityValue(String(format: "%016llx", session.frameChecksum))
            } else {
                VStack(spacing: 10) {
                    if !session.finished { ProgressView().tint(.white) }
                    Text(session.finished ? "No display frame was produced." : "Starting homebrew…")
                        .foregroundStyle(.white).multilineTextAlignment(.center)
                }.padding()
            }
        }
    }

    private func controlsOverlay(in geometry: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            HStack(alignment: .top, spacing: 10) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(name).font(.subheadline.bold()).lineLimit(1)
                    Text(session.paused ? "Paused" : session.status)
                        .font(.caption).foregroundStyle(session.fault ? .red : .white.opacity(0.85))
                        .lineLimit(2).accessibilityIdentifier("game-status")
                }.frame(maxWidth: .infinity, alignment: .leading)
                Button { session.setPaused(!session.paused) } label: {
                    SatchelRailLabel(session.paused ? "Resume" : "Pause", systemImage: session.paused ? "play.fill" : "pause.fill")
                }
                    .disabled(session.finished || session.stopping).accessibilityIdentifier("game-pause")
                Button { showingDiagnostics = true } label: { SatchelRailLabel("Diagnostics", systemImage: "info.circle") }
                    .accessibilityLabel("Diagnostics").accessibilityIdentifier("game-diagnostics")
                Button { session.stop() } label: { SatchelRailLabel("Stop", systemImage: "stop.fill") }
                    .disabled(session.finished || session.stopping)
                    .accessibilityLabel("Stop").accessibilityIdentifier("game-stop")
                Button { close() } label: { SatchelRailLabel("Done", systemImage: "xmark") }
                    .accessibilityIdentifier("game-done")
            }
            .font(.subheadline).buttonStyle(.plain).tint(.white)
            .padding(.horizontal, 16).padding(.vertical, 10)
            .background(.black.opacity(0.62), in: RoundedRectangle(cornerRadius: 14))

            Spacer(minLength: 4)

            HStack(alignment: .bottom, spacing: 8) {
                VStack(spacing: 4) {
                    control("▲", label: "Up", id: "game-up", bit: 13)
                    HStack(spacing: 4) {
                        control("◀", label: "Left", id: "game-left", bit: 12)
                        Color.clear.frame(width: 44, height: 44)
                        control("▶", label: "Right", id: "game-right", bit: 14)
                    }
                    control("▼", label: "Down", id: "game-down", bit: 15)
                }
                Spacer(minLength: 8)
                VStack(spacing: 2) {
                    Text("\(session.frames.formatted()) frames").accessibilityIdentifier("game-frame-count")
                    Text(session.lastGameFrameSeconds > 0 && !session.paused && !session.finished
                         ? String(format: "%.1f FPS", 1.0 / session.lastGameFrameSeconds) : "n/a FPS")
                        .accessibilityIdentifier("game-fps")
                }.font(.caption2.monospacedDigit()).foregroundStyle(.white.opacity(0.8))
                    .padding(.horizontal, 8).padding(.vertical, 5)
                    .background(.black.opacity(0.55), in: Capsule())
                Spacer(minLength: 8)
                VStack(spacing: 12) {
                    HStack(spacing: 10) {
                        control("B", label: "B", id: "game-b", bit: 1)
                        control("A", label: "A", id: "game-a", bit: 0)
                    }
                    control("+", label: "Plus", id: "game-plus", bit: 10)
                }
            }
        }
        .padding(.leading, max(12, geometry.safeAreaInsets.leading))
        .padding(.trailing, max(12, geometry.safeAreaInsets.trailing))
        .padding(.top, max(8, geometry.safeAreaInsets.top))
        .padding(.bottom, max(10, geometry.safeAreaInsets.bottom))
        .foregroundStyle(.white)
    }

    private var diagnostics: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    SatchelCard(title: "Execution") {
                        Text(session.status).font(.headline)
                            .contextMenu { Button("Copy status", systemImage: "doc.on.doc") { UIPasteboard.general.string = session.status } }
                            .fixedSize(horizontal: false, vertical: true)
                            .accessibilityIdentifier("player-diagnostics-status")
                        Text("\(session.frames.formatted()) guest display frames")
                        Text("\(session.instructions.formatted()) instructions • PC \(session.pc)")
                            .accessibilityIdentifier("game-instructions")
                        Text(session.lastGameFrameSeconds > 0
                             ? String(format: "Last guest frame: %.3f seconds (%.1f FPS)", session.lastGameFrameSeconds, 1.0 / session.lastGameFrameSeconds)
                             : "Guest frame interval not measured yet.")
                        Text(String(format: "Active interpreter time: %.3f seconds", session.activeInterpreterSeconds))
                        Text("Guest pixels and game logic execute in the interpreter. Unsupported services stop with an explicit diagnostic.")
                            .foregroundStyle(SatchelStyle.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .accessibilityElement(children: .contain)
                    .accessibilityIdentifier("player-diagnostics-card")
                    SatchelCard(title: "Debug output") {
                        Text(session.output.isEmpty ? "No debug output." : session.output)
                            .font(.system(.footnote, design: .monospaced)).textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                        Text("Framebuffer FNV-1a: \(String(format: "%016llx", session.frameChecksum))")
                            .font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    SatchelCard(title: "Run report") {
                        Button { session.saveReport() } label: { Label("Save run report", systemImage: "square.and.arrow.down").frame(minHeight: 44) }
                        if let url = session.reportURL { ShareLink("Share run report", item: url).frame(minHeight: 44) }
                        if let error = session.reportError { Text(error).foregroundStyle(SatchelStyle.coral) }
                    }
                }.font(.subheadline).lineSpacing(3).padding(20)
            }
            .background(SatchelStyle.ink.ignoresSafeArea())
            .navigationTitle("Run diagnostics").navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    SatchelRailButton("Done", systemImage: "xmark", identifier: "diagnostics-done") { showingDiagnostics = false }
                }.satchelVertical()
            }
        }.tint(SatchelStyle.mint).satchelMenuStatus().presentationDetents([.large])
    }

    private func close() { session.stop(); dismiss() }

    private func control(_ title: String, label: String, id: String, bit: UInt64) -> some View {
        let mask: UInt64 = 1 << bit
        return HomebrewHoldButton(title: title, label: label, identifier: id,
                                  held: session.heldButtons & mask != 0,
                                  enabled: !session.paused && !session.finished && !session.stopping,
                                  changed: { session.setButton(mask, pressed: $0) },
                                  accessiblePress: { session.accessiblePress(mask) })
            .frame(width: 44, height: 44)
    }
}
