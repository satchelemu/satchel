import Foundation
private let captureLock = NSLock()
private var capturedRequests = Set<String>()
func satchelCaptureRequest(_ request: SatchelShaderRequest) throws {
    guard let raw = getenv("DM_SHADER_REQUEST_CAPTURE") else { return }
    captureLock.lock(); defer { captureLock.unlock() }
    let normalized = try SatchelShaderTranslator.normalized(request)
    let identity = try SatchelShaderTranslator.identity(request, normalized: normalized)
    if capturedRequests.contains(identity) { return }
    let directory = URL(fileURLWithPath: String(cString: raw), isDirectory: true)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let value: [String: Any] = ["record": request.record.base64EncodedString(), "stage": request.stage.rawValue,
        "wanted": request.wantedPixelLocations.sorted(), "key": request.expectedKey ?? "",
        "request": identity, "recordSHA256": SatchelShaderTranslator.sha256(request.record)]
    try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]).write(to: directory.appendingPathComponent(identity + ".json"), options: .atomic)
    capturedRequests.insert(identity)
}
