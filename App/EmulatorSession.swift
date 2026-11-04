import Foundation
import QuartzCore
import UIKit

struct ExpectedResult: Decodable {
    let frames: Int
    let framebufferFNV1a: String
}

final class EmulatorSession: NSObject, ObservableObject {
    @Published var image: CGImage?
    @Published var score = 0
    @Published var lives = 3
    @Published var fps = 0.0
    @Published var millionInstructions = 0.0
    @Published var frameMilliseconds = 0.0
    @Published var frameCount = 0
    @Published var paddlePosition = 80
    @Published var paused = false
    @Published var assist = true
    @Published var status = "Running device validation…"
    @Published var error: String?
    @Published var validation: String = "Running · 600 frames"
    @Published var reportURL: URL?
    private var vm: OpaquePointer?
    private var link: CADisplayLink?
    private var touch = 80
    private var resetPending = false
    private var validationEligible = true
    private var expected: ExpectedResult?
    private var windowStart = 0.0
    private var windowFrames = 0
    private var windowInstructions: UInt64 = 0
    private var windowCPU = 0.0
    private var cpuSamples: [Double] = []
    private var totalCPU = 0.0
    private var startTime = 0.0

    override init() {
        super.init()
        if let url = Bundle.main.url(forResource: "expected", withExtension: "json"),
           let data = try? Data(contentsOf: url) {
            expected = try? JSONDecoder().decode(ExpectedResult.self, from: data)
        }
        restartValidation()
        link = CADisplayLink(target: self, selector: #selector(tick))
        link?.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
        link?.add(to: .main, forMode: .common)
    }

    deinit { link?.invalidate(); if let vm { satchel_destroy(vm) } }

    func restartValidation() {
        if let vm { satchel_destroy(vm) }
        vm = nil
        guard let url = Bundle.main.url(forResource: "brick", withExtension: "arm64"),
              let data = try? Data(contentsOf: url) else {
            error = "The bundled ARM64 test program is missing."; return
        }
        vm = data.withUnsafeBytes { bytes in
            satchel_create(bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count)
        }
        guard vm != nil else { error = "Could not allocate interpreter memory."; return }
        error = nil; frameCount = 0; score = 0; lives = 3; paused = false
        assist = true; validationEligible = true; resetPending = false; touch = 80
        validation = "Running · 600 frames"; status = "Running device validation…"
        cpuSamples = []; totalCPU = 0; startTime = CACurrentMediaTime()
        windowStart = startTime; windowFrames = 0; windowInstructions = 0; windowCPU = 0
    }

    func movePaddle(fraction: Double) {
        touch = Int(min(1, max(0, fraction)) * 159)
        assist = false
        interruptValidation()
    }

    func toggleAssist() { assist.toggle(); interruptValidation() }
    func resetGame() { resetPending = true; interruptValidation(); paused = false }
    func togglePause() {
        paused.toggle()
        windowStart = CACurrentMediaTime(); windowFrames = 0; windowCPU = 0
        if let vm { windowInstructions = satchel_instructions(vm) }
    }
    private func interruptValidation() {
        if frameCount < 600 && validationEligible {
            validationEligible = false
            validation = "Interrupted by player"
        }
        status = "Drag anywhere on the playfield"
    }

    @objc private func tick(_ sender: CADisplayLink) {
        guard !paused, error == nil, let vm else { return }
        if assist && frameCount > 0 { touch = Int(satchel_state(vm, 3)) }
        let start = CACurrentMediaTime()
        let result = satchel_frame(vm, Int32(touch), resetPending ? 1 : 0, 1_000_000)
        let elapsed = CACurrentMediaTime() - start
        resetPending = false
        guard result == SATCHEL_YIELD else {
            error = result == SATCHEL_BUDGET ? "Guest exceeded the one-million-instruction frame budget." : String(cString: satchel_error(vm))
            status = "Interpreter stopped safely"
            writeReport(); return
        }
        frameCount += 1; windowFrames += 1; windowCPU += elapsed; totalCPU += elapsed
        cpuSamples.append(elapsed * 1000)
        if cpuSamples.count > 3600 { cpuSamples.removeFirst(600) }
        score = Int(satchel_state(vm, 0)); lives = Int(satchel_state(vm, 1))
        paddlePosition = Int(satchel_state(vm, 5))
        let data = Data(bytes: satchel_pixels(vm), count: 160 * 240 * 4)
        if let provider = CGDataProvider(data: data as CFData) {
            image = CGImage(width: 160, height: 240, bitsPerComponent: 8, bitsPerPixel: 32,
                            bytesPerRow: 160 * 4, space: CGColorSpaceCreateDeviceRGB(),
                            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        }
        let now = CACurrentMediaTime()
        if now - windowStart >= 0.5 {
            fps = Double(windowFrames) / (now - windowStart)
            frameMilliseconds = windowCPU * 1000 / Double(max(1, windowFrames))
            millionInstructions = Double(satchel_instructions(vm) - windowInstructions) / max(windowCPU, 0.000001) / 1_000_000
            windowStart = now; windowFrames = 0; windowCPU = 0; windowInstructions = satchel_instructions(vm)
        }
        if frameCount == 600 && validationEligible {
            let actual = String(format: "%016llx", satchel_checksum(vm))
            if let expected, expected.frames == 600, expected.framebufferFNV1a == actual {
                validation = "Passed · 600 frames"
                status = "Device check passed. Your turn."
            } else {
                validation = "FAILED · framebuffer mismatch"
                error = "Device result did not match the native reference. Check the report."
            }
            writeReport()
        }
        if frameCount % 300 == 0 { writeReport() }
    }

    @discardableResult func writeReport() -> URL? {
        guard let vm else { return nil }
        let sorted = cpuSamples.sorted()
        let p95 = sorted.isEmpty ? 0 : sorted[min(sorted.count - 1, Int(Double(sorted.count) * 0.95))]
        let report: [String: Any] = [
            "app": "Satchel 0.2", "timestamp": ISO8601DateFormatter().string(from: Date()),
            "device": UIDevice.current.model, "systemVersion": UIDevice.current.systemVersion,
            "workload": "Original synthetic ARM64 brick-breaker; not a Switch game",
            "cpuExecution": "C interpreter; no JIT; guest bytes never executed natively",
            "guestABI": "Private framebuffer + SVC 0xF001; not Horizon OS",
            "validation": validation, "frames": frameCount,
            "instructions": satchel_instructions(vm), "interpreterSeconds": totalCPU,
            "elapsedSeconds": CACurrentMediaTime() - startTime,
            "interpreterMIPS": Double(satchel_instructions(vm)) / max(totalCPU, 0.000001) / 1_000_000,
            "recentFrameP95Milliseconds": p95, "recentPresentedFPS": fps,
            "framebufferFNV1a": String(format: "%016llx", satchel_checksum(vm)),
            "score": score, "lives": lives, "autoPlay": assist,
            "error": error ?? NSNull(),
            "limitations": ["ARM64 instruction subset", "No commercial Switch ROM loader", "Demo uses private ABI; NRO runner has initial Horizon services", "No NVN/Metal translation", "No audio emulation", "Not an App Store approval or Switch performance test"]
        ]
        do {
            let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Satchel-report.json")
            let data = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: url, options: .atomic)
            if frameCount == 600 && validationEligible {
                try data.write(to: url.deletingLastPathComponent().appendingPathComponent("Satchel-validation.json"), options: .atomic)
            }
            reportURL = url
            if frameCount == 600 { print("SATCHEL_REPORT " + String(decoding: data, as: UTF8.self)) }
            return url
        } catch { status = "Could not save report: \(error.localizedDescription)"; return nil }
    }
}
