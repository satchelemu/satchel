import SwiftUI

#if !SATCHEL_UI_PREVIEW_MINIMAL
/// Reads the same title overrides used by launch and the native menu commands.
struct GamePlayerSettingsPanel: View {
    let titleID: String
    @State private var values: [String: String] = [:]
    @State private var overrides: [String: String] = [:]
    @State private var failure: String?

    private struct Choice: Identifiable {
        let key: String
        let title: String
        let options: [(String, String)]
        var id: String { key }
        func label(_ value: String) -> String {
            if let label = options.first(where: { $0.0 == value })?.1 { return label }
            if key == "DM_RUMBLE_GAIN" || key == "DM_RUMBLE_MIN", let number = Double(value) {
                return number == 0 && key == "DM_RUMBLE_MIN" ? "Off" : "\(Int((number * 100).rounded()))%"
            }
            if key == "DM_RENDER_SCALE", let scale = Double(value) { return "\(Int((scale * 100).rounded()))%" }
            return value
        }
    }
    private let choices = [
        Choice(key: "DM_FPS_TARGET", title: "FPS target", options: [("0", "Game default"), ("30", "30 FPS"), ("60", "60 FPS")]),
        Choice(key: "DM_RENDER_SCALE", title: "Render scale", options: [("1.0", "100%"), ("0.85", "85%"), ("0.75", "75%"), ("0.65", "65%"), ("0.5", "50%")]),
        Choice(key: "DM_SCREEN_MODE", title: "Screen mode", options: [("fit", "Fit"), ("stretch", "Stretch")]),
        Choice(key: "DM_HAPTICS", title: "Haptics", options: [("on", "On"), ("off", "Off")]),
        Choice(key: "DM_RUMBLE_GAIN", title: "Rumble strength", options: [("0", "0%"), ("0.5", "50%"), ("1.0", "100%"), ("1.5", "150%"), ("2.0", "200%"), ("3.0", "300%")]),
        Choice(key: "DM_RUMBLE_MIN", title: "Rumble cutoff", options: [("0", "Off"), ("0.2", "20%"), ("0.45", "45%"), ("0.6", "60%")]),
        Choice(key: "DM_PIPELINE_MODE", title: "Pipeline mode", options: [("exact", "Exact"), ("async", "Async (experimental)"), ("skip", "Skip (experimental)"), ("hybrid", "Hybrid"), ("legacy", "Legacy")])
    ]

    var body: some View {
        SatchelPanel(title: "Game settings") {
            ForEach(choices) { choice in
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text(choice.title).font(.system(size: 14, weight: .bold, design: .rounded))
                        Spacer()
                        Menu {
                            ForEach(choice.options, id: \.0) { option in
                                Button(option.1) { save(choice.key, option.0) }
                            }
                        } label: {
                            Label(choice.label(values[choice.key] ?? ""), systemImage: "chevron.up.chevron.down")
                                .frame(minHeight: 44)
                        }
                        .accessibilityIdentifier("game-setting-" + choice.key)
                        .accessibilityValue(values[choice.key] ?? "")
                    }
                    let global = SatchelPlayerGlobalDefaults()[choice.key] ?? ""
                    let titleDefault = SatchelPlayerDefaults(titleID)[choice.key] ?? global
                    Text("Global default: \(choice.label(global))" + (titleDefault == global ? "" : " · Title default: \(choice.label(titleDefault))"))
                        .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
                        .accessibilityIdentifier("game-default-" + choice.key)
                    HStack {
                        Text(overrides[choice.key] == nil ? "Using default" : "Override")
                            .foregroundStyle(overrides[choice.key] == nil ? SatchelTheme.textDim : SatchelTheme.spark)
                            .accessibilityIdentifier("game-override-" + choice.key)
                        Spacer()
                        if overrides[choice.key] != nil {
                            Button("Reset to default") {
                                if !SatchelResetPlayerSetting(titleID, choice.key) { failure = "This setting could not be reset." }
                                reload()
                            }.accessibilityIdentifier("game-reset-" + choice.key)
                        }
                    }.font(.system(size: 12, weight: .semibold, design: .rounded))
                }
                if choice.key != "DM_PIPELINE_MODE" { Divider() }
            }
            Text("FPS target, render scale and pipeline mode apply next time you launch this game. Screen mode, haptics, rumble strength and rumble cutoff also change a paused game. Rumble strength scales controller rumble and the phone's haptics; cutoff silences a controller's weakest rumble. A forced frame target may need a compatible game patch.")
                .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
            Text("Exact preserves rendering accuracy. Async is experimental: it uses equivalent ready pipelines and waits when required, and has not yet been shown to reduce stutter. Skip is experimental and can leave objects missing while pipelines build. Hybrid can reduce pipeline waits but may change rendering. Legacy uses the older pipeline path.")
                .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
            if let failure { Text(failure).foregroundStyle(SatchelTheme.spark) }
        }
        .onAppear(perform: reload)
        .onChange(of: titleID) { _, _ in reload() }
        .onReceive(NotificationCenter.default.publisher(for: .SatchelPlayerSettingsDidChange)) { _ in reload() }
    }

    private func reload() {
        values = SatchelSavedPlayerSettings(titleID)
        overrides = SatchelPlayerOverrides(titleID)
    }
    private func save(_ key: String, _ value: String) {
        failure = SatchelSavePlayerSettings(titleID, [key: value]) ? nil : "This setting could not be saved."
        reload()
    }
}
#endif
