import SwiftUI

private let ink = Color(red: 0.035, green: 0.055, blue: 0.075)
private let mint = Color(red: 0.51, green: 0.93, blue: 0.82)

struct LabView: View {
    @StateObject private var session = EmulatorSession()
    @State private var showDetails = false
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        GeometryReader { geometry in
            VStack(spacing: 14) {
                HStack(alignment: .center) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(AppBrand.name.uppercased() + " / LAB").font(.system(size: 25, weight: .black, design: .rounded)).tracking(-1)
                        Text("ARM64 INTERPRETER EXPERIMENT").font(.system(size: 9, weight: .semibold, design: .monospaced)).tracking(1.4).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text("NO JIT").font(.system(size: 10, weight: .bold, design: .monospaced))
                        .padding(.horizontal, 10).padding(.vertical, 7)
                        .foregroundStyle(mint).background(mint.opacity(0.12), in: Capsule())
                }

                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("01 / BRICK BREAKER").font(.system(size: 11, weight: .bold, design: .monospaced)).foregroundStyle(mint)
                        Text("Original test program").font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text(String(format: "%04d", session.score)).font(.system(size: 27, weight: .medium, design: .monospaced))
                }

                ZStack {
                    RoundedRectangle(cornerRadius: 18).fill(Color(red: 0.063, green: 0.075, blue: 0.106))
                    if let image = session.image {
                        Image(decorative: image, scale: 1).interpolation(.none).resizable().scaledToFit().padding(8).accessibilityHidden(true)
                    }
                    if session.paused {
                        Text("PAUSED").font(.system(.title2, design: .monospaced, weight: .bold))
                            .padding(20).background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                    }
                    if let error = session.error {
                        VStack(spacing: 12) {
                            Image(systemName: "exclamationmark.octagon").font(.largeTitle)
                            Text(error).font(.callout).multilineTextAlignment(.center)
                        }.padding(24).background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14)).padding()
                    }
                }
                .aspectRatio(2 / 3, contentMode: .fit)
                .frame(maxHeight: max(180, geometry.size.height - 310))
                .frame(maxWidth: .infinity)
                .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder(.white.opacity(0.08)))
                .contentShape(Rectangle())
                .overlay {
                    GeometryReader { playfield in
                        Color.clear.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0).onChanged { value in
                            session.movePaddle(fraction: value.location.x / playfield.size.width)
                        })
                        .accessibilityElement(children: .ignore)
                        .accessibilityLabel("Brick breaker playfield. Drag left or right to move the paddle.")
                        .accessibilityIdentifier("playfield")
                        .accessibilityValue("Paddle \(session.paddlePosition), lives \(session.lives)")
                    }
                }

                HStack(spacing: 0) {
                    metric(String(format: "%.0f", session.fps), "FPS")
                    metric(String(format: "%.1f", session.frameMilliseconds), "MS / GUEST FRAME")
                    metric(String(format: "%.1f", session.millionInstructions), "M INSTRUCTIONS / S")
                }
                .padding(.vertical, 12).background(.white.opacity(0.035), in: RoundedRectangle(cornerRadius: 12))

                HStack(spacing: 10) {
                    Button { session.togglePause() } label: {
                        Image(systemName: session.paused ? "play.fill" : "pause.fill").frame(width: 20)
                    }.accessibilityLabel(session.paused ? "Resume" : "Pause").accessibilityIdentifier("pause")
                    Button { session.resetGame() } label: { Image(systemName: "arrow.counterclockwise").frame(width: 20) }.accessibilityLabel("Restart game")
                    Button { session.toggleAssist() } label: {
                        HStack(spacing: 6) {
                            Image(systemName: "sparkle")
                            Text(session.assist ? "Auto play" : "Manual").font(.system(size: 12, weight: .semibold))
                        }.frame(maxWidth: .infinity)
                    }.tint(session.assist ? mint : .gray)
                    Button { session.writeReport(); showDetails = true } label: { Image(systemName: "waveform.path.ecg").frame(width: 20) }.accessibilityLabel("Diagnostics")
                }
                .buttonStyle(.bordered).controlSize(.large).tint(mint)

                VStack(spacing: 4) {
                    Text(session.status).font(.system(size: 12, weight: .medium)).foregroundStyle(mint)
                    Text("Synthetic workload · Switch games are not supported yet")
                        .font(.system(size: 10)).foregroundStyle(.secondary)
                }.multilineTextAlignment(.center)
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 22).padding(.top, 10)
            .frame(maxWidth: 520).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .background(ink.ignoresSafeArea())
        .onChange(of: scenePhase) { _, phase in
            if phase != .active && !session.paused { session.togglePause() }
        }
        .sheet(isPresented: $showDetails) {
            NavigationStack {
                List {
                    Section("Device validation") {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Result").font(.caption).foregroundStyle(.secondary)
                            Text(session.validation).font(.headline)
                        }.accessibilityElement(children: .contain)
                        LabeledContent("Rendered frames", value: "\(session.frameCount)")
                        Text("The first 600 auto-play frames are checked against an independently compiled native version of the same original C program.")
                            .font(.footnote).foregroundStyle(.secondary)
                        Button("Run 600-frame check again") { session.restartValidation(); showDetails = false }
                    }
                    Section("What is running") {
                        Text("Real ARM64 instruction bytes interpreted by C. Gameplay and every framebuffer pixel are computed by the guest. SwiftUI displays the resulting image and supplies touch input.")
                        Text("No JIT, executable-memory mapping, downloaded native modules, or debugger attachment is used by this app.")
                    }
                    Section("What this does not prove") {
                        Text("This is an instruction subset and a private demo ABI, not a Switch emulator. This demo uses no Nintendo code or Horizon services. The separate NRO runner has initial homebrew support; neither path has an NVN renderer or emulated audio. These measurements do not predict Switch game performance or App Store approval.")
                    }
                    if let url = session.reportURL {
                        Section { ShareLink("Share device report", item: url) }
                    }
                }
                .navigationTitle("Lab notes")
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { showDetails = false } } }
            }
            .tint(mint)
        }
    }

    private func metric(_ value: String, _ label: String) -> some View {
        VStack(spacing: 4) {
            Text(value).font(.system(size: 21, weight: .medium, design: .monospaced)).monospacedDigit()
            Text(label).font(.system(size: 7, weight: .semibold, design: .monospaced)).foregroundStyle(.secondary)
        }.frame(maxWidth: .infinity)
    }
}
