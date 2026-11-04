import Foundation
import Metal

@available(iOS 18.0, macOS 15.0, *)
private final class RuntimeShaderBox: @unchecked Sendable {
    let lock = NSLock()
    var service: SatchelShaderPublicationService?
}
@available(iOS 18.0, macOS 15.0, *)
private let runtimeShaderBox = RuntimeShaderBox()

@available(iOS 18.0, macOS 15.0, *)
public enum SatchelShaderRuntime {
    public static func install(_ service: SatchelShaderPublicationService?) {
        runtimeShaderBox.lock.lock(); runtimeShaderBox.service = service; runtimeShaderBox.lock.unlock()
    }
    private static func service() -> SatchelShaderPublicationService? {
        runtimeShaderBox.lock.lock(); defer { runtimeShaderBox.lock.unlock() }
        return runtimeShaderBox.service
    }
    fileprivate static func current() -> SatchelShaderPublicationService? { service() }
}

@available(iOS 18.0, macOS 15.0, *)
private final class RuntimeShaderSnapshot {
    let service: SatchelShaderPublicationService
    let publication: SatchelShaderPublication
    init(_ service: SatchelShaderPublicationService, _ publication: SatchelShaderPublication) {
        self.service = service; self.publication = publication
    }
}

private func runtimeString(_ pointer: UnsafePointer<CChar>?, _ count: Int, maximum: Int) -> String? {
    guard let pointer, count > 0, count <= maximum else { return nil }
    return String(data: Data(bytes: pointer, count: count), encoding: .utf8)
}
private func runtimeCopy(_ text: String, _ output: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int64 {
    let data = Array(text.utf8)
    guard let output, capacity > data.count else { return -1 }
    for (i, byte) in data.enumerated() { output[i] = CChar(bitPattern: byte) }
    output[data.count] = 0
    return Int64(data.count)
}

@_cdecl("satchel_shader_runtime_prepare")
public func satchelShaderRuntimePrepare(_ control: UnsafeRawPointer?, _ controlSize: Int,
    _ code: UnsafeRawPointer?, _ codeSize: Int, _ key: UnsafePointer<CChar>?, _ keySize: Int,
    _ facts: UnsafeRawPointer?, _ factsSize: Int, _ locations: UInt32, _ flags: UInt32) -> Int32 {
    satchelShaderRuntimePrepareSnapshot(control, controlSize, code, codeSize, key, keySize,
        facts, factsSize, locations, flags, nil)
}

@_cdecl("satchel_shader_runtime_prepare_snapshot")
public func satchelShaderRuntimePrepareSnapshot(_ control: UnsafeRawPointer?, _ controlSize: Int,
    _ code: UnsafeRawPointer?, _ codeSize: Int, _ key: UnsafePointer<CChar>?, _ keySize: Int,
    _ facts: UnsafeRawPointer?, _ factsSize: Int, _ locations: UInt32, _ flags: UInt32,
    _ snapshot: UnsafeMutablePointer<UnsafeMutableRawPointer?>?) -> Int32 {
    snapshot?.pointee = nil
    guard #available(iOS 18.0, macOS 15.0, *), let service = SatchelShaderRuntime.current() else { return -1 }
    guard let control, let code, (0x760...1024 * 1024).contains(controlSize), (32...16 * 1024 * 1024).contains(codeSize),
          (0...1024 * 1024).contains(factsSize), facts != nil || factsSize == 0,
          keySize == 16, let key = runtimeString(key, keySize, maximum: 16), flags & ~3 == 0 else { return -2 }
    return autoreleasepool {
        do {
            let ctl = Data(bytes: control, count: controlSize), data = Data(bytes: code, count: codeSize)
            let stage = try SatchelShaderTranslator.stage(control: ctl, code: data)
            let query = factsSize == 0 ? nil : facts.map { Data(bytes: $0, count: factsSize) }
            let wanted = Set((0..<32).filter { locations & (UInt32(1) << $0) != 0 })
            let request = try SatchelShaderRequest(control: ctl, code: data, stage: stage, factsJSON: query,
                wantedPixelLocations: wanted, mappingOnly: flags & 1 != 0, dualSource: flags & 2 != 0,
                skippedControlRanges: service.skippedControlRanges, expectedKey: key)
            let publication = try service.prepare(request)
            if let snapshot {
                snapshot.pointee = Unmanaged.passRetained(RuntimeShaderSnapshot(service, publication)).toOpaque()
            }
            return stage == .vertex ? 0 : stage == .fragment ? 1 : stage == .geometry ? 2 : 5
        } catch SatchelShaderError.unsupportedStage { return -3 }
        catch { return -4 }
    }
}

@_cdecl("satchel_shader_runtime_snapshot")
public func satchelShaderRuntimeSnapshot(_ key: UnsafePointer<CChar>?, _ size: Int) -> UnsafeMutableRawPointer? {
    guard #available(iOS 18.0, macOS 15.0, *), size == 16,
          let key = runtimeString(key, size, maximum: 16), let service = SatchelShaderRuntime.current(),
          let publication = service.current(key: key) else { return nil }
    return Unmanaged.passRetained(RuntimeShaderSnapshot(service, publication)).toOpaque()
}
@_cdecl("satchel_shader_runtime_release_snapshot")
public func satchelShaderRuntimeReleaseSnapshot(_ snapshot: UnsafeMutableRawPointer?) {
    guard #available(iOS 18.0, macOS 15.0, *), let snapshot else { return }
    Unmanaged<RuntimeShaderSnapshot>.fromOpaque(snapshot).release()
}
@_cdecl("satchel_shader_runtime_directory")
public func satchelShaderRuntimeDirectory(_ snapshot: UnsafeMutableRawPointer?, _ output: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int64 {
    guard #available(iOS 18.0, macOS 15.0, *), let snapshot else { return -1 }
    return runtimeCopy(Unmanaged<RuntimeShaderSnapshot>.fromOpaque(snapshot).takeUnretainedValue().publication.directory.path, output, capacity)
}
@_cdecl("satchel_shader_runtime_identity")
public func satchelShaderRuntimeIdentity(_ snapshot: UnsafeMutableRawPointer?, _ output: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int64 {
    guard #available(iOS 18.0, macOS 15.0, *), let snapshot else { return -1 }
    return runtimeCopy(Unmanaged<RuntimeShaderSnapshot>.fromOpaque(snapshot).takeUnretainedValue().publication.requestSHA256, output, capacity)
}
@_cdecl("satchel_shader_runtime_revision")
public func satchelShaderRuntimeRevision(_ snapshot: UnsafeMutableRawPointer?) -> UInt64 {
    guard #available(iOS 18.0, macOS 15.0, *), let snapshot else { return 0 }
    return Unmanaged<RuntimeShaderSnapshot>.fromOpaque(snapshot).takeUnretainedValue().publication.revision
}
@_cdecl("satchel_shader_runtime_library")
public func satchelShaderRuntimeLibrary(_ snapshot: UnsafeMutableRawPointer?, _ variant: UnsafePointer<CChar>?, _ size: Int) -> UnsafeMutableRawPointer? {
    guard #available(iOS 18.0, macOS 15.0, *), let snapshot,
          let variant = runtimeString(variant, size, maximum: 3) else { return nil }
    let state = Unmanaged<RuntimeShaderSnapshot>.fromOpaque(snapshot).takeUnretainedValue()
    do {
        let result = try state.service.library(state.publication, variant: variant)
        return Unmanaged.passRetained(result.library as AnyObject).toOpaque()
    } catch { return nil }
}
