import SwiftUI
import UIKit

// Only owned immutable images cross from the graphics worker to this canvas.
struct SwitchRuntimeSurface: View {
    let state: SwitchRuntimeSurfaceState
    @ObservedObject var presenter: SwitchRuntimeFramePresenter
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        RuntimeSurfaceRepresentable(state: state, presenter: presenter, frame: presenter.frame,
                                    sceneActive: scenePhase == .active)
            .accessibilityIdentifier("runtime-game-surface")
    }
}

private struct RuntimeSurfaceRepresentable: UIViewRepresentable {
    let state: SwitchRuntimeSurfaceState
    let presenter: SwitchRuntimeFramePresenter
    let frame: SwitchRuntimeFrame?
    let sceneActive: Bool

    func makeUIView(context: Context) -> RuntimeSurfaceView {
        RuntimeSurfaceView(state: state, presenter: presenter)
    }

    func updateUIView(_ view: RuntimeSurfaceView, context: Context) {
        view.configure(state: state, presenter: presenter, sceneActive: sceneActive)
    }

    static func dismantleUIView(_ view: RuntimeSurfaceView, coordinator: ()) {
        view.dismantle()
    }
}

@MainActor private final class RuntimeSurfaceView: UIView {
    private var state: SwitchRuntimeSurfaceState
    private var presenter: SwitchRuntimeFramePresenter
    private let imageView = UIImageView()
    private let label = UILabel()
    private var binding: UInt64?
    private weak var attachedWindow: UIWindow?
    private var requestedActive = false
    private var sceneSuspended = true
    private var dismantled = false

    init(state: SwitchRuntimeSurfaceState, presenter: SwitchRuntimeFramePresenter) {
        self.state = state
        self.presenter = presenter
        binding = state.beginBinding()
        super.init(frame: .zero)
        backgroundColor = .black
        isOpaque = true
        imageView.contentMode = .scaleAspectFit
        imageView.isOpaque = true
        imageView.backgroundColor = .black
        imageView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(imageView)
        label.text = "Waiting for game frame"
        label.textColor = UIColor(white: 0.75, alpha: 1)
        label.font = .preferredFont(forTextStyle: .subheadline)
        label.adjustsFontForContentSizeCategory = true
        label.textAlignment = .center
        label.numberOfLines = 0
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)
        NSLayoutConstraint.activate([
            imageView.leadingAnchor.constraint(equalTo: leadingAnchor),
            imageView.trailingAnchor.constraint(equalTo: trailingAnchor),
            imageView.topAnchor.constraint(equalTo: topAnchor),
            imageView.bottomAnchor.constraint(equalTo: bottomAnchor),
            label.centerXAnchor.constraint(equalTo: centerXAnchor),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
            label.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 12),
            label.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -12)
        ])
        let center = NotificationCenter.default
        center.addObserver(self, selector: #selector(sceneDeactivated(_:)), name: UIScene.willDeactivateNotification, object: nil)
        center.addObserver(self, selector: #selector(sceneDeactivated(_:)), name: UIScene.didEnterBackgroundNotification, object: nil)
        center.addObserver(self, selector: #selector(sceneActivated(_:)), name: UIScene.didActivateNotification, object: nil)
    }

    required init?(coder: NSCoder) { fatalError("Use init(state:)") }

    func configure(state: SwitchRuntimeSurfaceState, presenter: SwitchRuntimeFramePresenter, sceneActive: Bool) {
        guard !dismantled else { return }
        if self.state !== state || self.presenter !== presenter {
            if let binding { self.state.endBinding(binding) }
            clearImage()
            self.state = state
            self.presenter = presenter
            binding = state.beginBinding()
        }
        requestedActive = sceneActive
        publishAttachment()
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if attachedWindow !== window {
            if let binding { state.detach(binding: binding) }
            clearImage()
            attachedWindow = window
            sceneSuspended = window?.windowScene?.activationState != .foregroundActive
        }
        publishAttachment()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        publishAttachment()
    }

    @objc private func sceneDeactivated(_ notification: Notification) {
        guard let scene = notification.object as? UIScene, scene === window?.windowScene else { return }
        sceneSuspended = true
        if let binding { state.detach(binding: binding) }
        clearImage()
    }

    @objc private func sceneActivated(_ notification: Notification) {
        guard let scene = notification.object as? UIScene, scene === window?.windowScene else { return }
        sceneSuspended = false
        publishAttachment()
    }

    private func publishAttachment() {
        guard !dismantled, let binding else { return }
        guard requestedActive, !sceneSuspended, let window, let scene = window.windowScene,
              scene.activationState == .foregroundActive,
              !window.isHidden, !isHidden, alpha > 0,
              bounds.width.isFinite, bounds.height.isFinite,
              bounds.width > 0, bounds.height > 0 else {
            state.detach(binding: binding)
            clearImage()
            return
        }
        let width = ceil(bounds.width * scene.screen.scale)
        let height = ceil(bounds.height * scene.screen.scale)
        guard width.isFinite, height.isFinite, width > 0, height > 0,
              width <= CGFloat(UInt32.max), height <= CGFloat(UInt32.max) else {
            state.detach(binding: binding)
            clearImage()
            return
        }
        state.update(binding: binding, pixelWidth: UInt32(width), pixelHeight: UInt32(height), attached: true)
        displayCurrentImage()
    }

    private func displayCurrentImage() {
        guard let frame = presenter.frame, state.validate(epoch: frame.epoch) else {
            imageView.image = nil
            label.isHidden = false
            return
        }
        if imageView.image?.cgImage !== frame.image {
            imageView.image = UIImage(cgImage: frame.image, scale: 1, orientation: .up)
        }
        label.isHidden = true
    }

    private func clearImage() {
        imageView.image = nil
        label.isHidden = false
        presenter.surfaceInvalidated()
    }

    func dismantle() {
        guard !dismantled else { return }
        dismantled = true
        NotificationCenter.default.removeObserver(self)
        if let binding { state.endBinding(binding) }
        clearImage()
        binding = nil
        attachedWindow = nil
    }
}
