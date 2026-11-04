import SwiftUI

// Engine-bound pieces of the game detail: the on-phone executable check (VIXL
// interpreter diagnostics) and the experimental game display. Kept apart from
// the library UI so the screens can be previewed without the engine.
struct PackageDiagnostics: View {
    @ObservedObject var library: HomebrewLibrary
    let item: HomebrewItem
    @StateObject private var runtime = SwitchRuntimeSession()
    @State private var pickingKeys = false
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            runtimeCheck
            SatchelCard(title: "Game display") {
                SwitchRuntimeSurface(state: runtime.surface, presenter: runtime.framePresenter)
                    .aspectRatio(16.0 / 9.0, contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 16))
            }
        }
        .fileImporter(isPresented: $pickingKeys, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { runtime.importKeys(url) }
            case .failure: runtime.error = "The console key file could not be opened."
            }
        }
        .onChange(of: runtime.keysAvailable) { _, available in
            if available { NotificationCenter.default.post(name: .satchelKeysChanged, object: nil) }
        }
        .alert("Runtime check stopped", isPresented: Binding(get: { runtime.error != nil }, set: { if !$0 { runtime.error = nil } })) {
            Button("OK") { runtime.error = nil }
        } message: { Text(runtime.error ?? "") }
        .onDisappear { runtime.stop() }
        .onChange(of: scenePhase) { _, phase in if phase != .active { runtime.stop() } }
    }

    private var runtimeCheck: some View {
        SatchelCard(title: "On-phone executable check") {
            Text("Checks the original program inside this local package. It does not prepare a separate app or contact a computer.")
                .font(.subheadline).foregroundStyle(SatchelStyle.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if runtime.busy {
                ProgressView()
                Button("Stop check") { runtime.stop() }.frame(minHeight: 44)
            } else {
                Button(runtime.keysAvailable ? "Replace console keys…" : "Import console keys…") { pickingKeys = true }
                    .frame(minHeight: 44).accessibilityIdentifier("import-console-keys")
                if runtime.keysAvailable {
                    Text("Keys stored securely on this device").font(.caption).foregroundStyle(SatchelStyle.mint)
                    Button("Find programs") { runtime.scan(library.localFile(item)) }
                        .frame(minHeight: 44).accessibilityIdentifier("find-package-programs")
                    ForEach(runtime.programs, id: \.titleID) { program in
                        Text(NativeGameProfile.supportMessage(titleID: program.titleID))
                            .font(.caption).foregroundStyle(SatchelStyle.secondary)
                            .accessibilityIdentifier("native-profile-status-" + program.titleID)
                        Button("Check title " + program.titleID) {
                            runtime.check(library.localFile(item), expectedSHA256: item.id, titleID: program.titleID)
                        }.font(.system(.subheadline, design: .monospaced))
                            .frame(minHeight: 44).accessibilityIdentifier("check-title-" + program.titleID)
                    }
                }
            }
            if let status = runtime.status {
                Text(status).font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-status")
            }
            if let report = runtime.report {
                Text(report.cpuOutcome).font(.headline)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-outcome")
                Text("\(report.instructions.formatted()) instructions • \(report.activeModule ?? "guest") PC \(report.pc)")
                    .font(.system(.caption, design: .monospaced)).accessibilityIdentifier("runtime-instructions")
                if let vi = report.vi {
                    Text("Display service: \(vi.returnedCalls) calls completed • \(vi.liveLayers) live layers • \(vi.entryCount) checked entries")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-display-service")
                }
                Text("\(report.memoryQueries.formatted()) memory queries serviced • Game graphics not connected")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .accessibilityIdentifier("runtime-memory-queries")
                Text("\(report.infoCallsServiced.formatted()) process information calls serviced")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                Text("\(report.ipcRequests.formatted()) service requests attempted")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-ipc-attempts")
                Text("\(report.ipcServiced.formatted()) service requests completed • \(report.smSessions.formatted()) service-manager sessions")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-ipc")
                Text("\(report.sessionCloses.formatted()) server sessions closed • \(report.closedSessionHandles.formatted()) client handles awaiting closure")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-session-closes")
                Text("\(report.storageReads.formatted()) game storage reads completed • \(report.storageBytes.formatted()) bytes delivered to the guest")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-storage")
                if let control = report.applicationControl {
                    Text(control.attached ? "Application control metadata verified and attached" : "Application control metadata not attached")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-application-control")
                }
                if let parental = report.parentalControl {
                    Text("\(parental.initializedChildren.formatted()) emulated parental-control sessions initialized")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-parental-initialized")
                }
                if let time = report.time, time.configured {
                    Text("Local clock initialized")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-clock-source")
                    Text("\(time.sharedMappings.formatted()) shared clock mappings")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-clock-mappings")
                }
                if let assets = report.romFS {
                    Text("\(assets.files.formatted()) RomFS files indexed • \(assets.sampledBytes.formatted()) asset bytes verified")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-romfs")
                } else if let error = report.romFSError {
                    Text("Asset storage check: " + error)
                        .font(.caption).foregroundStyle(SatchelStyle.secondary).fixedSize(horizontal: false, vertical: true)
                }
                Text(report.compiledVulkanCompute.passed ? "On-device shader → Vulkan passed (synthetic)" : "On-device shader → Vulkan stopped at stage \(report.compiledVulkanCompute.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-compiled")
                Text(report.graphicsSelfTest.passed ? "Shader compiler self-test passed (synthetic)" : "Shader compiler self-test stopped at stage \(report.graphicsSelfTest.status)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-shader-probe")
                Text(report.vulkanSelfTest.passed ? "Vulkan self-test passed (synthetic)" : "Vulkan self-test stopped at stage \(report.vulkanSelfTest.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-probe")
                Text(report.vulkanLifecycle.passed ? "Interpreted Vulkan lifecycle passed (synthetic)" : "Interpreted Vulkan lifecycle stopped at stage \(report.vulkanLifecycle.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-lifecycle")
                Text(report.vulkanBuffers.passed ? "Vulkan buffer storage passed (synthetic)" : "Vulkan buffer storage stopped at stage \(report.vulkanBuffers.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-buffers")
                Text(report.vulkanTransfers.passed ? "Vulkan GPU transfers passed (synthetic)" : "Vulkan GPU transfers stopped at stage \(report.vulkanTransfers.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-transfers")
                Text(report.vulkanPools.passed ? "Vulkan guest-memory pools passed (synthetic)" : "Vulkan guest-memory pools stopped at stage \(report.vulkanPools.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-pools")
                Text(report.interpretedVulkanPools.memory.passed ? "Interpreted Vulkan pools passed (synthetic)" : "Interpreted Vulkan pools stopped at stage \(report.interpretedVulkanPools.memory.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-pool-dispatch")
                Text(report.interpretedVulkanTransfers.memory.passed ? "Interpreted Vulkan transfers passed (synthetic)" : "Interpreted Vulkan transfers stopped at stage \(report.interpretedVulkanTransfers.memory.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-transfer-dispatch")
                Text(report.interpretedVulkanCompute.passed ? "Interpreted Vulkan compute passed (synthetic)" : "Interpreted Vulkan compute stopped at stage \(report.interpretedVulkanCompute.stage)")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-compute-dispatch")
                if let raster = report.interpretedVulkanRaster {
                    Text(raster.memory.passed ? "Interpreted Vulkan drawing passed (synthetic)" : "Interpreted Vulkan drawing stopped at stage \(raster.memory.stage)")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-raster-dispatch")
                }
                if let captured = report.capturedVulkan {
                    Text(captured.passed ? "Captured shader → Vulkan passed (synthetic)" : "Captured shader check stopped at stage \(captured.stage), status \(captured.status)")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-captured")
                }
                if let provoking = report.provokingVulkan {
                    Text(provoking.passed ? "Vulkan flat-shading modes passed (synthetic)" : "Vulkan flat-shading check stopped at stage \(provoking.stage), status \(provoking.status)")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-provoking")
                }
                if let textureFrame = report.textureFrameVulkan {
                    Text(textureFrame.passed ? "Vulkan texture → frame passed (synthetic)" : "Texture-to-frame check stopped at stage \(textureFrame.stage), status \(textureFrame.status)")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-vulkan-texture-frame")
                }
                if let permissions = report.memoryPermissions {
                    Text("\(permissions.serviced) memory-permission requests serviced • \(permissions.requests) attempted")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-memory-permissions")
                    Text("\(permissions.changedBytes) bytes cumulatively reprotected")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-memory-permission-bytes")
                }
                if let attributes = report.memoryAttributes {
                    Text("\(attributes.serviced) memory-attribute requests serviced • \(attributes.requests) attempted")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-memory-attributes")
                    Text("\(attributes.permissionLockedBytes) bytes with locked permissions")
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                        .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-memory-locked-bytes")
                }
                Text("\(report.appletMessages.received) application messages received • \(report.appletMessages.pending) pending")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-applet-messages")
                Text("\(report.mutexes.waits) mutex waits • \(report.mutexes.handoffs) ownership handoffs • \(report.mutexes.currentWaiters) pending")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-mutexes")
                Text("\(report.scheduler.threads.count) guest thread objects • \(report.scheduler.threads.filter { $0.instructions > 0 }.count) have executed")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary)
                    .fixedSize(horizontal: false, vertical: true).accessibilityIdentifier("runtime-threads")
                Text("The shader self-test uses original test data. It is not a rendered game frame; game graphics are not connected yet.")
                    .font(.caption).foregroundStyle(SatchelStyle.secondary).fixedSize(horizontal: false, vertical: true)
                Text("\(report.modules.count) NSO modules decoded • \(report.modules.reduce(0) { $0 + $1.segmentHashesChecked }) segment hashes verified")
                    .font(.caption).foregroundStyle(SatchelStyle.mint)
                    .accessibilityIdentifier("runtime-modules")
                ForEach(report.modules, id: \.name) { module in
                    Text(module.name + " • " + ByteCountFormatter.string(fromByteCount: Int64(module.imageBytes), countStyle: .file))
                        .font(.caption).foregroundStyle(SatchelStyle.secondary)
                }
            }
        }.accessibilityElement(children: .contain).accessibilityIdentifier("runtime-check-card")
    }
}

/// Full-screen homebrew player (interpreter).
struct HomebrewPlayerCover: View {
    let url: URL
    let name: String
    let sha256: String
    var body: some View { HomebrewPlayer(url: url, name: name, expectedSHA256: sha256) }
}

/// The interpreter demo from the workshop.
struct InterpreterDemoView: View {
    let close: () -> Void
    var body: some View {
        NavigationStack {
            LabView().toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Library") { close() } } }
        }.tint(SatchelStyle.mint)
    }
}
