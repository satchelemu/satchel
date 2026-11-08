import SwiftUI

/// One answer to a confirmation. `id` is its accessibility identifier.
struct SatchelConfirmAction: Identifiable {
    enum Role { case normal, destructive, cancel }
    let id: String
    let title: String
    var role: Role = .normal
    var systemImage: String?
    let action: () -> Void
}

/// A question over the whole screen, answered by touch or controller. A cancel answer is
/// added when none is given; B on a controller and a tap outside the card also cancel.
struct SatchelConfirmRequest: Identifiable {
    let id = UUID()
    let title: String
    let message: String
    var actions: [SatchelConfirmAction]
    /// Accessibility identifier of the card, for journeys.
    var identifier = "confirm-card"
}

private struct SatchelConfirmKey: EnvironmentKey { static let defaultValue: ((SatchelConfirmRequest) -> Void)? = nil }
extension EnvironmentValues {
    /// Shows a confirmation over the screen that hosts it (every panel and the home screen).
    var satchelConfirm: ((SatchelConfirmRequest) -> Void)? {
        get { self[SatchelConfirmKey.self] }
        set { self[SatchelConfirmKey.self] = newValue }
    }
}

/// Hands its content the confirmation of the screen it sits in. A screen's own body is outside
/// the host its chrome installs, so controls there read it through this.
struct SatchelConfirmReader<Content: View>: View {
    @Environment(\.satchelConfirm) private var confirm
    @ViewBuilder let content: (@escaping (SatchelConfirmRequest) -> Void) -> Content
    var body: some View {
        content { request in
            if let confirm { confirm(request) }
            else { assertionFailure("No confirmation host above \(request.title)") }
        }
    }
}

extension View {
    /// Hosts confirmations asked for by this view or anything inside it, drawn over all of it.
    func satchelConfirmHost() -> some View { modifier(SatchelConfirmHost()) }
}

private struct SatchelConfirmHost: ViewModifier {
    @State private var request: SatchelConfirmRequest?

    func body(content: Content) -> some View {
        content
            .environment(\.satchelConfirm, { next in
                Haptics.tap()
                withAnimation(SatchelTheme.snap) { request = next }
            })
            .accessibilityHidden(request != nil)
            .overlay {
                if let request {
                    SatchelConfirmCard(request: request) { answer in
                        withAnimation(SatchelTheme.glide) { self.request = nil }
                        answer?()
                    }
                    .transition(.opacity)
                }
            }
    }
}

private struct SatchelConfirmCard: View {
    let request: SatchelConfirmRequest
    /// Closes the card, then runs the chosen answer (nil for cancel without one).
    let finish: ((() -> Void)?) -> Void

    private var actions: [SatchelConfirmAction] {
        request.actions.contains { $0.role == .cancel } ? request.actions
            : request.actions + [SatchelConfirmAction(id: "confirm-cancel", title: "Cancel", role: .cancel) {}]
    }
    private var cancel: SatchelConfirmAction? { actions.first { $0.role == .cancel } }

    var body: some View {
        ZStack {
            Color.black.opacity(0.72).ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture { finish(cancel?.action) }
                .accessibilityHidden(true)
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text(request.title).font(SatchelTheme.display(21))
                        .foregroundStyle(SatchelTheme.text)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityAddTraits(.isHeader)
                    Text(request.message).font(.system(size: 14, weight: .medium, design: .rounded))
                        .foregroundStyle(SatchelTheme.textDim)
                        .fixedSize(horizontal: false, vertical: true)
                    VStack(spacing: 10) {
                        ForEach(actions) { answer in
                            Button { finish(answer.action) } label: {
                                SatchelButtonLabel(title: answer.title, systemImage: answer.systemImage, kind: kind(answer), height: 46, fill: true)
                            }
                            .buttonStyle(PressableStyle())
                            .accessibilityIdentifier(answer.id)
                            .controllerAction(answer.role == .cancel ? "confirm-cancel" : "confirm-" + answer.id,
                                              style: .button(.capsule, inset: -4)) { finish(answer.action) }
                        }
                    }
                    .padding(.top, 4)
                }
                .padding(22)
                .frame(maxWidth: 440, alignment: .leading)
                .background(SatchelTheme.surfaceHigh, in: RoundedRectangle(cornerRadius: 24, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: 24, style: .continuous).strokeBorder(SatchelTheme.line) }
                .shadow(color: .black.opacity(0.5), radius: 30, y: 14)
                .padding(.horizontal, 24).padding(.vertical, 20)
                .frame(maxWidth: .infinity)
                .accessibilityElement(children: .contain)
                // The screen underneath is hidden from accessibility while the card is up, so
                // VoiceOver stays on the question without a modal trait (which UI tests treat
                // as a system alert and dismiss on their own).
                .accessibilityIdentifier(request.identifier)
            }
            .scrollBounceBehavior(.basedOnSize)
            .defaultScrollAnchor(.center)
        }
        // Only the question takes controller input while it is up; B cancels.
        .controllerNavigation(modal: true, back: { finish(cancel?.action) })
    }

    private func kind(_ answer: SatchelConfirmAction) -> SatchelButtonKind {
        switch answer.role {
        case .destructive: return .danger
        case .cancel: return .quiet
        case .normal: return .secondary
        }
    }
}
