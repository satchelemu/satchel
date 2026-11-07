import SwiftUI

/// JSON controller profiles live alongside each title's player settings in the preferences plist.
struct ControllersView: View {
    var titleID = ""
    @State private var category = "Xbox"
    @State private var mapping: [String: String] = [:]
    @State private var deadZone = 0.15
    @State private var shortcut = "home"
    @State private var error: String?
    @ObservedObject private var input = ControllerInputMonitor.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(titleID.isEmpty ? "Global defaults" : "This game: \(titleID)")
                .font(.headline).accessibilityIdentifier("controller-profile-scope")
            Text("Profiles use GameController input names. Unchanged games inherit the global profile for their controller category.")
                .font(.caption).foregroundStyle(SatchelTheme.textDim)
            pickerRow("Controller category") {
                Picker("Controller category", selection: $category) {
                    ForEach(SatchelControllerCategories(), id: \.self) { Text($0).tag($0) }
                }.labelsHidden().accessibilityIdentifier("controller-category")
            }
            SatchelPanel(title: "Live input", tint: SatchelTheme.tide) {
                Text(input.connected ? "Connected: \(input.category)" : "Connect a controller to test inputs")
                    .accessibilityIdentifier("controller-connected")
                HStack(spacing: 24) {
                    stick("Left stick", x: "lx", y: "ly")
                    stick("Right stick", x: "rx", y: "ry")
                }
                Text(SatchelControllerInputs().filter { (input.values[$0]?.doubleValue ?? 0) >= 0.5 }.joined(separator: ", "))
                    .font(.caption.monospaced()).frame(minHeight: 20)
                    .accessibilityIdentifier("controller-live-input")
            }
            diagnostics
            SatchelPanel(title: "Stick dead zone") {
                Text("\(Int(deadZone * 100))% (both sticks)").accessibilityIdentifier("controller-dead-zone-value")
                Slider(value: Binding(get: { deadZone }, set: { deadZone = $0; save() }), in: 0...0.5, step: 0.01).accessibilityLabel("Stick dead zone")
                    .accessibilityIdentifier("controller-dead-zone")
                Text("Small movements inside the circle are ignored. Movement beyond it scales smoothly to full tilt.").font(.caption)
            }
            SatchelPanel(title: "Pause menu shortcut") {
                pickerRow("Hold for 0.75 seconds") {
                    Picker("Hold for 0.75 seconds", selection: Binding(get: { shortcut }, set: { shortcut = $0; save() })) {
                        Text("Home button").tag("home")
                        Text("Menu + Options").tag("menu-options")
                        Text("Off").tag("off")
                    }.labelsHidden().accessibilityIdentifier("controller-shortcut")
                }
                Text("Home is reserved. Without Home, use Menu + Options. Each button works normally alone. Once both are down, both stay blocked until both are released; hold them together for 0.75 seconds to open the menu. The first button can reach the game before the second is pressed. A controller with neither Home nor Options opens the menu by holding Menu alone, which still reaches the game as Plus. Off disables the shortcut.")
                    .font(.caption).foregroundStyle(SatchelTheme.textDim)
            }
            SatchelPanel(title: "Physical input → Switch button") {
                ForEach(SatchelControllerInputs(), id: \.self) { name in
                    pickerRow(name) {
                        Picker(name, selection: Binding(get: { mapping[name] ?? "Unmapped" }, set: { mapping[name] = $0; save() })) {
                            ForEach(SatchelControllerOutputs(), id: \.self) { Text($0).tag($0) }
                        }.labelsHidden().accessibilityIdentifier("controller-map-" + name)
                    }
                }
                Text("D-pad entries are the Direction Pad element's four buttons. Sticks keep their left and right axes. A shortcut takes priority over its mapping.")
                    .font(.caption).foregroundStyle(SatchelTheme.textDim)
            }
            Button(titleID.isEmpty ? "Reset category to factory defaults" : "Use global default for this category") {
                SatchelResetControllerProfile(titleID, category); load()
            }.accessibilityIdentifier("controller-reset")
            if let error { Text(error).foregroundStyle(.red) }
        }
        .onAppear { load(); input.showDiagnostics(true) }
        .onDisappear { input.showDiagnostics(false) }
        .onChange(of: category) { _, _ in load() }
    }
    /// Exactly what iOS reports, so a pad that does nothing can be told apart from one
    /// that iOS never handed to the app.
    private var diagnostics: some View {
        SatchelPanel(title: "What iOS reports", tint: SatchelTheme.tide) {
            if input.devices.isEmpty {
                Text("No game controller reported. Check that Settings > Bluetooth shows it connected and that the controller is in its Xbox or iOS mode, then press a button.")
                    .accessibilityIdentifier("controller-diagnostic-none")
            }
            ForEach(Array(input.devices.enumerated()), id: \.element.id) { index, device in
                VStack(alignment: .leading, spacing: 3) {
                    Text(device.name + (device.current ? " (current)" : "")).font(.headline)
                    Text("iOS category: \(device.productCategory). Satchel uses: \(device.satchelCategory). Profile: \(device.profile).")
                    Text(device.details).foregroundStyle(SatchelTheme.textDim)
                    Text("Pressed: " + (device.pressed.isEmpty ? "nothing" : device.pressed)).font(.caption.monospaced())
                    Text(device.axes).font(.caption.monospaced())
                    Text("Input changes seen: \(device.changes)").font(.caption.monospaced())
                }
                .font(.caption)
                .accessibilityElement(children: .combine)
                .accessibilityIdentifier("controller-diagnostic-\(index)")
            }
            if !input.otherInputs.isEmpty { Text(input.otherInputs).font(.caption) }
            Text(Self.declaration).font(.caption).foregroundStyle(SatchelTheme.textDim)
                .accessibilityIdentifier("controller-diagnostic-plist")
            if !input.history.isEmpty {
                Text(input.history.joined(separator: "\n")).font(.caption2.monospaced()).foregroundStyle(SatchelTheme.textDim)
                    .accessibilityIdentifier("controller-diagnostic-history")
            }
        }
    }
    /// The installed build's own Info.plist, so a stale or hand-edited install is visible.
    static var declaration: String {
        let info = Bundle.main.infoDictionary ?? [:]
        let interaction = (info["GCSupportsControllerUserInteraction"] as? Bool) == true ? "yes" : "missing"
        let profiles = (info["GCSupportedGameControllers"] as? [[String: Any]])?.compactMap { $0["ProfileName"] as? String } ?? []
        return "This build declares controller support: \(interaction); profiles: \(profiles.isEmpty ? "missing" : profiles.joined(separator: ", "))."
    }
    private func load() {
        let profile = SatchelControllerProfile(titleID, category)
        mapping = profile["mapping"] as? [String: String] ?? [:]
        deadZone = (profile["deadZone"] as? NSNumber)?.doubleValue ?? 0.15
        shortcut = profile["shortcut"] as? String ?? "home"
    }
    private func save() {
        guard mapping.count == SatchelControllerInputs().count else { return }
        error = SatchelSaveControllerProfile(titleID, category, ["version": 1, "mapping": mapping,
            "deadZone": deadZone, "shortcut": shortcut]) ? nil : "The controller profile could not be saved."
    }
    /// A menu picker shows only its value outside a form, so the title goes beside it.
    private func pickerRow<Content: View>(_ title: String, @ViewBuilder picker: () -> Content) -> some View {
        HStack(spacing: 12) {
            Text(title).font(.subheadline.weight(.semibold)).foregroundStyle(SatchelTheme.text)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 8)
            picker().fixedSize()
        }
    }

    private func stick(_ title: String, x: String, y: String) -> some View {
        let dx = input.values[x]?.doubleValue ?? 0, dy = input.values[y]?.doubleValue ?? 0
        return VStack {
            ZStack {
                Circle().stroke(SatchelTheme.textDim, lineWidth: 2)
                Circle().fill(SatchelTheme.tide.opacity(0.2)).frame(width: 90 * deadZone, height: 90 * deadZone)
                Circle().fill(SatchelTheme.spark).frame(width: 12, height: 12).offset(x: dx * 39, y: -dy * 39)
            }.frame(width: 90, height: 90)
            Text(title).font(.caption)
            Text(String(format: "%.2f, %.2f", dx, dy)).font(.caption.monospaced())
                .accessibilityIdentifier("controller-stick-" + x)
        }.accessibilityElement(children: .ignore)
            .accessibilityLabel(title)
            .accessibilityValue(String(format: "%.2f, %.2f", dx, dy))
            .accessibilityIdentifier("controller-stick-" + x)
    }
}

struct ControllerLayoutView: View {
    let titleID: String
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        PanelChrome(identifier: "close-controller-layout", close: { dismiss() }) {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("Controller layout").font(SatchelTheme.display(26))
                    Text("Switch positions: X above, B below, Y left, A right. L / ZL and R / ZR are the shoulders and triggers. L3 and R3 press the sticks.")
                    // Each label keeps its line, so the diagram holds its shape on the front screen.
                    HStack(alignment: .center, spacing: 24) {
                        VStack(spacing: 6) { Text("L / ZL"); Text("Left stick + L3"); Text("D-pad") }
                        VStack(spacing: 4) {
                            HStack(spacing: 28) { Text("Minus"); Text("Plus") }
                            Text("X")
                            HStack(spacing: 28) { Text("Y"); Text("A") }
                            Text("B")
                        }
                        VStack(spacing: 6) { Text("R / ZR"); Text("Right stick + R3") }
                    }
                    .font(.subheadline.monospaced().weight(.semibold)).lineLimit(1).fixedSize()
                    .padding(.vertical, 8)
                    Text("Touch: 2D shows the left stick and face buttons. 3D adds the D-pad, stick clicks and a right stick where you touch the right third. The layout button at the top of the game cycles layouts.")
                    ControllersView(titleID: titleID)
                }.padding(24)
            }.background(SatchelTheme.shell)
        }
        .controllerNavigation(back: { dismiss() })
    }
}
