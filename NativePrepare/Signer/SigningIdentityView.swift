import SwiftUI
import UniformTypeIdentifiers

struct SigningIdentityView: View {
    @State private var importing = false
    @State private var selected: URL?
    @State private var passphrase = ""
    @State private var name: String?
    @State private var team = ""
    @State private var expiry: Date?
    @State private var message = ""
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.satchelConfirm) private var confirm

    var body: some View {
        SatchelPanel(title: "Signing identity", tint: SatchelTheme.spark) {
            if scenePhase == .active {
                if let name, let expiry {
                    let expired = expiry <= Date(), expiring = expiry.timeIntervalSinceNow < 30 * 86400
                    let tint = expired ? SatchelTheme.ember : expiring ? SatchelTheme.spark : SatchelTheme.leaf
                    HStack(alignment: .top, spacing: 12) {
                        Image(systemName: expired ? "xmark.seal.fill" : "checkmark.seal.fill")
                            .font(.system(size: 20, weight: .bold)).foregroundStyle(tint)
                            .frame(width: 42, height: 42).background(tint.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                        VStack(alignment: .leading, spacing: 3) {
                            Text(name).font(.system(size: 15, weight: .bold, design: .rounded))
                                .fixedSize(horizontal: false, vertical: true).privacySensitive()
                            Text("Team: \(team)").font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim).privacySensitive()
                            Text("Expires: \(expiry.formatted(date: .abbreviated, time: .shortened))")
                                .font(.system(size: 12, weight: .semibold, design: .rounded))
                                .foregroundStyle(expired || expiring ? tint : SatchelTheme.textDim)
                        }
                    }
                    if expired {
                        banner("This certificate has expired. Import a renewed identity before preparing games.", tint: SatchelTheme.ember)
                    } else if expiring {
                        banner("This certificate expires within 30 days. Renew it and import the new identity. Games signed with an expired certificate may stop launching.", tint: SatchelTheme.spark)
                    }
                } else {
                    Text("Import your Apple Development identity once to sign games on this phone. Required team: \(SatchelSigningIdentity.applicationTeam ?? "unknown, this build is not signed").")
                        .font(.system(size: 14, weight: .medium, design: .rounded))
                        .fixedSize(horizontal: false, vertical: true)
                }
                if selected != nil {
                    VStack(alignment: .leading, spacing: 8) {
                        SecureField("PKCS#12 passphrase", text: $passphrase)
                            .textContentType(.password).autocorrectionDisabled().privacySensitive()
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                            .padding(.horizontal, 14).frame(minHeight: 46)
                            .background(SatchelTheme.surfaceHigh, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
                            .overlay { RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(SatchelTheme.spark.opacity(0.4)) }
                            .accessibilityIdentifier("identity-passphrase")
                        HStack(spacing: 8) {
                            Button { save() } label: {
                                SatchelButtonLabel(title: "Save identity on this device", systemImage: "lock.fill", kind: .primary, height: 40)
                            }
                            .buttonStyle(PressableStyle())
                            .accessibilityIdentifier("identity-save")
                            Button { clearImport() } label: {
                                SatchelButtonLabel(title: "Cancel import", kind: .quiet, height: 40)
                            }
                            .buttonStyle(PressableStyle())
                        }
                    }
                }
                ViewThatFits(in: .horizontal) {
                    HStack(spacing: 8) { actions }
                    VStack(alignment: .leading, spacing: 8) { actions }
                }
                Text("The identity stays in this device's Keychain, available only while unlocked. Its passphrase is never saved. Remove your exported .p12 from Files and Recently Deleted after importing it.")
                    .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
                if !message.isEmpty {
                    Text(message).font(.system(size: 12, weight: .semibold, design: .rounded))
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("identity-message")
                }
            } else {
                Text("Unlock Satchel to manage your signing identity.")
                    .font(.system(size: 14, weight: .medium, design: .rounded))
            }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            do {
                selected = try result.get().first
                passphrase = ""; message = "Enter the PKCS#12 passphrase on this phone."
            } catch { message = "Identity selection cancelled or unavailable." }
        }
        .onAppear { refresh() }
        .onDisappear { clearImport() }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active { clearImport() } else { refresh() }
        }
    }

    @ViewBuilder private var actions: some View {
        Button { importing = true } label: {
            SatchelButtonLabel(title: name == nil ? "Import signing identity" : "Replace signing identity",
                              systemImage: "square.and.arrow.down", kind: name == nil ? .accent(SatchelTheme.ember) : .secondary, height: 40)
        }
        .buttonStyle(PressableStyle())
        .accessibilityIdentifier("identity-import")
        .controllerAction("identity-import", style: .button(.capsule)) { importing = true }
        if name != nil {
            Button { askRemove() } label: {
                SatchelButtonLabel(title: "Remove signing identity", systemImage: "trash", kind: .danger, height: 40)
            }
            .buttonStyle(PressableStyle())
            .accessibilityIdentifier("identity-remove")
            .controllerAction("identity-remove", style: .button(.capsule)) { askRemove() }
        }
    }

    private func banner(_ text: String, tint: Color) -> some View {
        Label(text, systemImage: "exclamationmark.triangle.fill")
            .font(.system(size: 12, weight: .semibold, design: .rounded)).foregroundStyle(tint)
            .fixedSize(horizontal: false, vertical: true)
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(tint.opacity(0.12), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    private func clearImport() { passphrase = ""; selected = nil }
    /// Removing the certificate stops Prepare until another is imported, so it asks first.
    private func askRemove() {
        guard let confirm else { return }
        confirm(SatchelConfirmRequest(title: "Remove your signing identity?",
            message: "\(AppBrand.name) cannot prepare games until you import a signing identity again. Games you already prepared are not changed. Your .p12 file in Files is not touched.",
            actions: [SatchelConfirmAction(id: "confirm-identity-remove", title: "Remove signing identity", role: .destructive, systemImage: "trash") { remove() },
                      SatchelConfirmAction(id: "cancel-identity-remove", title: "Keep it", role: .cancel) {}],
            identifier: "identity-remove-confirm"))
    }
    private func remove() {
        do { try SatchelSigningIdentity.removeStoredIdentity(); message = "Current signing identity removed. Retained migration copies are unchanged." }
        catch { message = error.localizedDescription }
        refresh()
    }
    private func refresh() {
        name = nil; team = ""; expiry = nil
        do {
            let identity = try SatchelSigningIdentity.storedIdentity()
            name = identity.certificateName; team = identity.team; expiry = identity.expiry
        } catch { message = error.localizedDescription }
    }
    private func save() {
        guard let selected else { return }
        defer { clearImport() }
        let access = selected.startAccessingSecurityScopedResource()
        defer { if access { selected.stopAccessingSecurityScopedResource() } }
        do {
            let input = try FileHandle(forReadingFrom: selected)
            defer { try? input.close() }
            var data = try input.read(upToCount: 1024 * 1024 + 1) ?? Data()
            defer { data.resetBytes(in: 0..<data.count) }
            guard data.count <= 1024 * 1024 else { throw PrepareFailure(message: "Choose a PKCS#12 identity smaller than 1 MiB.") }
            guard let team = SatchelSigningIdentity.applicationTeam else { throw PrepareFailure(message: "This build's signing team is unknown. Install a signed build before importing an identity.") }
            let identity = try SatchelSigningIdentity(pkcs12: data, passphrase: passphrase, team: team)
            passphrase = ""
            try identity.store()
            message = "Signing identity saved on this device."
            refresh()
        } catch { message = error.localizedDescription }
    }
}
