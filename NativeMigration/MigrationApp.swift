import Security
import SwiftUI

/// A one-time bridge build has no library or game view, so its source cannot change during export.
enum SatchelMigration {
    static let group = "group.com.dell.satchel.migration"
    /// The shared Keychain group carries the app's own team prefix, read from the private group.
    static var keychainGroup: String { (privateGroup.split(separator: ".").first.map(String.init) ?? "") + ".com.dell.satchel.migration" }
    static var bridge: Bool { Bundle.main.object(forInfoDictionaryKey: "SatchelMigrationBridge") as? Bool == true }
    static var privateGroup: String { Bundle.main.object(forInfoDictionaryKey: "SatchelPrivateKeychainGroup") as? String ?? "" }
    static func store() throws -> MigrationStore {
        guard let shared = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: group) else {
            throw MigrationFailure(message: "The signing profile does not grant Satchel's migration App Group. Install both migration-enabled builds from the same team before transferring data.")
        }
        let fm = FileManager.default
        return MigrationStore(shared: shared.appendingPathComponent("DevToSatchel-v1"),
                              documents: fm.urls(for: .documentDirectory, in: .userDomainMask)[0],
                              state: fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("SatchelMigration-v1"))
    }
    static func playerSetting(_ key: String) -> Bool {
        key.hasPrefix("SatchelPlayerSettings.") || key.hasPrefix("InGameSettings.") || key == "SatchelLiveControls"
    }
    static let credentials = [("com.dell.satchel.signing-identity", "pkcs12-v1"),
                              ("com.dell.satchel.console-keys", "nca-loader-v1")]
    static func query(_ service: String, _ account: String, _ group: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
         kSecAttrAccount as String: account, kSecAttrAccessGroup as String: group, kSecAttrSynchronizable as String: false]
    }
    static func readSecret(_ query: [String: Any]) throws -> Data? {
        var request = query
        request[kSecReturnData as String] = true
        request[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(request as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = result as? Data else {
            throw MigrationFailure(message: "Secure storage is unavailable (status \(status)). Unlock the phone and check both signing profiles, then retry.")
        }
        return data
    }
    static func saveSecret(_ data: Data, _ query: [String: Any], replace: Bool = false) throws {
        if let existing = try readSecret(query) {
            if existing == data { return }
            guard replace else { throw MigrationFailure(message: "A different migration credential already exists. Both original Keychains are retained; recovery needs review.") }
            let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
            guard status == errSecSuccess else { throw MigrationFailure(message: "Secure storage update failed (status \(status)).") }
        } else {
            var insert = query
            insert[kSecValueData as String] = data
            insert[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
            let status = SecItemAdd(insert as CFDictionary, nil)
            guard status == errSecSuccess else { throw MigrationFailure(message: "Secure storage handoff failed (status \(status)).") }
        }
        guard try readSecret(query) == data else { throw MigrationFailure(message: "Secure storage verification failed. Original credentials are retained.") }
    }
    static func exportMetadata(_ store: MigrationStore) throws {
        var present: [String] = []
        for (service, account) in credentials {
            if let data = try readSecret(query(service, account, privateGroup)) {
                try saveSecret(data, query(service, account, keychainGroup))
                present.append(service)
            }
        }
        let defaults = UserDefaults.standard.persistentDomain(forName: Bundle.main.bundleIdentifier!) ?? [:]
        let selected = defaults.filter { playerSetting($0.key) }
        try store.write(PropertyListSerialization.data(fromPropertyList: selected, format: .binary, options: 0), store.shared.appendingPathComponent("PlayerSettings.plist"))
        try store.write(JSONEncoder().encode(present), store.shared.appendingPathComponent("Credentials.json"))
    }
    static func adoptMetadata(_ store: MigrationStore) throws {
        let present = try JSONDecoder().decode([String].self, from: Data(contentsOf: store.shared.appendingPathComponent("Credentials.json")))
        for (service, account) in credentials where present.contains(service) {
            guard let data = try readSecret(query(service, account, keychainGroup)) else {
                throw MigrationFailure(message: "A shared credential is missing. Keep Satchel Dev installed and retry after unlocking.")
            }
            let local = query(service, account, privateGroup)
            if let old = try readSecret(local), old != data {
                try saveSecret(old, query(service + ".before-migration", account, privateGroup))
            }
            try saveSecret(data, local, replace: true)
        }
        let data = try Data(contentsOf: store.shared.appendingPathComponent("PlayerSettings.plist"))
        guard let defaults = try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              defaults.keys.allSatisfy(playerSetting) else { throw MigrationFailure(message: "The player settings snapshot is invalid.") }
        let backup = store.state.appendingPathComponent("OriginalPlayerSettings.plist")
        if !store.exists(backup) {
            let old = (UserDefaults.standard.persistentDomain(forName: Bundle.main.bundleIdentifier!) ?? [:]).filter { playerSetting($0.key) }
            try store.write(PropertyListSerialization.data(fromPropertyList: old, format: .binary, options: 0), backup)
        }
        for (key, value) in defaults { UserDefaults.standard.set(value, forKey: key) }
        guard UserDefaults.standard.synchronize() else { throw MigrationFailure(message: "Player settings could not be saved. Retry before retiring Satchel Dev.") }
        let saved = UserDefaults.standard.persistentDomain(forName: Bundle.main.bundleIdentifier!) ?? [:]
        for (key, value) in defaults {
            guard let actual = saved[key], NSDictionary(dictionary: [key: actual]).isEqual(to: [key: value]) else {
                throw MigrationFailure(message: "Player settings verification failed.")
            }
        }
    }
    /// Satchel Dev's games and saves are copied into Satchel from the Mac (devicectl); the in-app
    /// snapshot of 50 GB kept failing on the phone. Satchel adopts only what Dev handed over through the
    /// shared Keychain group and App Group (signing identity, console keys, player settings), once, then
    /// removes the shared folder so a partial snapshot's clones no longer hold storage.
    static func adoptHandoff() {
        guard !bridge, let store = try? store() else { return }
        let marker = store.state.appendingPathComponent("handoff-adopted")
        if !store.exists(marker) {
            guard store.exists(store.shared.appendingPathComponent("Credentials.json")),
                  store.exists(store.shared.appendingPathComponent("PlayerSettings.plist")) else { return }
            do {
                try store.locked { try adoptMetadata(store) }
                try store.write(Data("1".utf8), marker)
            } catch {
                recordFailure((error as? MigrationFailure)?.message ?? "Handoff: \((error as NSError).domain) \((error as NSError).code)")
                return
            }
        }
        do { if store.exists(store.shared) { try FileManager.default.removeItem(at: store.shared) } }
        catch { recordFailure("Cleanup: \((error as NSError).domain) \((error as NSError).code)") }
    }
    /// The last failure, without paths, for the Mac to read with devicectl (Library/Application Support).
    static func recordFailure(_ text: String) {
        let url = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("migration-error.txt")
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? Data((ISO8601DateFormatter().string(from: Date()) + " " + text + "\n").utf8).write(to: url, options: .atomic)
    }
    static func run(progress: @escaping (String) -> Void) throws {
        let store = try store(); store.progress = progress
        try store.locked {
            if bridge {
                // A published snapshot is immutable, including its settings and credential receipt.
                if !store.ready { try exportMetadata(store) }
                try store.exportDocuments()
            } else {
                if !store.complete {
                    try store.adoptDocuments()
                    try adoptMetadata(store)
                }
                try store.finish()
            }
        }
    }
}

struct SatchelRootView: View {
    var body: some View {
        // Only the temporary Satchel Dev bridge shows the migration screen; Satchel opens its library.
        if SatchelMigration.bridge { MigrationView {} }
        else { HomebrewView().task { await Task.detached(priority: .utility) { SatchelMigration.adoptHandoff() }.value } }
    }
}

struct MigrationView: View {
    var openLibrary: () -> Void
    @State private var busy = false
    @State private var done = false
    @State private var message = ""
    @State private var failed = false
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text(SatchelMigration.bridge ? "Move to Satchel" : "Bring your games into Satchel").font(.largeTitle.bold())
                Text("Keep both apps installed and this screen open and unlocked. Games, shader packs, imports, saves, player settings and signing credentials stay on this iPhone. The original data is retained until you verify the migration.")
                if !SatchelMigration.bridge {
                    Text("Your two libraries will be merged. Dev saves and player settings become active. Existing Satchel saves and other replaced files stay in its migration rollback folder. Packages with matching names get separate entries.")
                }
                if busy { ProgressView() }
                Text(message).accessibilityIdentifier("migration-status")
                if done {
                    if SatchelMigration.bridge {
                        Text("Snapshot verified. Completely close Satchel, then reopen it to finish. Do not use an older Dev build or remove either app yet.")
                    } else {
                        Text("Transfer verified. Open your games and check the saves and signing identity before removing Satchel Dev. Rollback snapshots are retained.")
                        Button("Open library", action: openLibrary).accessibilityIdentifier("migration-open-library")
                    }
                } else {
                    Button(failed ? "Resume migration" : "Start migration") { start() }
                        .disabled(busy).accessibilityIdentifier("migration-start")
                }
            }.padding(36).frame(maxWidth: 850, alignment: .leading)
        }
        .background(SatchelTheme.shell).foregroundStyle(SatchelTheme.text)
        .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
    }
    private func start() {
        busy = true; failed = false
        message = "Checking migration storage. No original data will be deleted."
        UIApplication.shared.isIdleTimerDisabled = true
        DispatchQueue.global(qos: .utility).async {
            do {
                try SatchelMigration.run { text in DispatchQueue.main.async { message = text } }
                DispatchQueue.main.async { busy = false; done = true; message = "Verification complete."; UIApplication.shared.isIdleTimerDisabled = false }
            } catch {
                DispatchQueue.main.async {
                    busy = false; failed = true
                    // Never surface paths or secret-bearing framework diagnostics: only the error's domain and codes.
                    let ns = error as NSError
                    let underlying = (ns.userInfo[NSUnderlyingErrorKey] as? NSError).map { " / \($0.domain) \($0.code)" } ?? ""
                    let code = "Error code: \(ns.domain) \(ns.code)\(underlying)."
                    message = (error as? MigrationFailure)?.message ?? "Migration stopped. Originals and rollback snapshots are retained. Unlock the phone, check free space and retry. " + code
                    SatchelMigration.recordFailure((error as? MigrationFailure)?.message ?? code)
                    UIApplication.shared.isIdleTimerDisabled = false
                }
            }
        }
    }
}
