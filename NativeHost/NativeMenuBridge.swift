import Combine
import SwiftUI

/// Shared adapter for Satchel's SwiftUI menu. All engine state stays in the native bridge.
@MainActor
final class NativeMenuBridge: ObservableObject {
    @Published private(set) var snapshot = SatchelMenuState()
    private var observation: AnyCancellable?

    init() {
        observation = NotificationCenter.default.publisher(for: .SatchelMenuDidChange)
            .sink { [weak self] _ in self?.snapshot = SatchelMenuState() }
    }

    @discardableResult
    func perform(_ action: String, value: String? = nil) -> Bool {
        SatchelMenuPerform(action, value)
    }

    /// Retain the hosting controller in the caller while its view is installed.
    func installMenuView(_ view: UIView?) {
        SatchelMenuSetCustomView(view)
    }
}
