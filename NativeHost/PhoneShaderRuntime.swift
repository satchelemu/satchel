import Foundation
import Metal

/// Translation workers own value snapshots. First use joins the exact request;
/// a draw never substitutes a pending or differently padded shader.
final class PhoneShaderRuntime: @unchecked Sendable {
    static let shared = PhoneShaderRuntime()
    private let lock = NSRecursiveLock()
    private var store: SatchelShaderStore?
    private var compiler: SatchelShaderCompiler?
    private var resources: ShaderWarmupResources?
    private struct Entry {
        let record: String
        var request: String
        var wanted: UInt32
        var inputs: UInt32
        var shader: SatchelTranslatedShader?
        var bytes: Int
        var touched: UInt64
    }
    private var entries: [String: Entry] = [:]
    private var sourceBytes = 0
    private var clock: UInt64 = 0

    func resetSession() {
        lock.lock(); defer { lock.unlock() }
        entries.removeAll(keepingCapacity: false)
        resources?.endSession()
        store = nil; compiler = nil; resources = nil; sourceBytes = 0; clock = 0
    }

    private func configure() throws {
        if store != nil { return }
        guard let raw = getenv("DM_PHONE_SHADER_CACHE") else {
            throw SatchelShaderError.invalidRequest
        }
        let resources = try ShaderWarmupResources.get(directory: URL(fileURLWithPath: String(cString: raw), isDirectory: true))
        self.resources = resources; store = resources.store; compiler = resources.compiler
        resources.warmKnown()
    }
    func prefetch(key: String, control: Data, code: Data) {
        // Registration is a hint. Never block a recording thread behind a draw.
        guard lock.try() else { return }
        defer { lock.unlock() }
        do {
            try configure()
            let stage = try SatchelShaderTranslator.stage(control: control, code: code)
            let mask = stage == .vertex ? resources!.mask(key) : 0
            let request = try SatchelShaderRequest(control: control, code: code, stage: stage,
                wantedPixelLocations: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 }), expectedKey: key)
            resources!.prefetch(request)
        } catch { } // Required first use reports any failure through prepare.
    }
    private func trim() {
        while sourceBytes > 64 * 1024 * 1024,
              let victim = entries.filter({ $0.value.shader != nil }).min(by: { $0.value.touched < $1.value.touched })?.key {
            sourceBytes -= entries[victim]!.bytes; entries[victim]!.shader = nil
        }
    }
    private func shader(_ key: String) throws -> SatchelTranslatedShader {
        guard var entry = entries[key] else { throw SatchelShaderError.invalidRequest }
        clock &+= 1; entry.touched = clock
        if let shader = entry.shader { entries[key] = entry; return shader }
        guard let shader = try store?.load(requestSHA256: entry.request) else { throw SatchelShaderCacheError.corruptEntry }
        entry.shader = shader; sourceBytes += entry.bytes; entries[key] = entry; trim()
        return shader
    }
    func prepare(key: String, control: Data, code: Data, wanted: UInt32) -> Int32 {
        lock.lock(); defer { lock.unlock() }
        do {
            try configure()
            guard entries.count < 65536 || entries[key] != nil else { throw SatchelShaderCacheError.insufficientSpace }
            let stage = try SatchelShaderTranslator.stage(control: control, code: code)
            let mask = stage == .vertex ? (entries[key]?.wanted ?? 0) | wanted | (resources?.mask(key) ?? 0) : 0
            let locations = Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 })
            let request = try SatchelShaderRequest(control: control, code: code, stage: stage,
                                                  wantedPixelLocations: locations, expectedKey: key)
            let record = NativePreparationRecipe.hash(request.record)
            if let old = entries[key] {
                guard old.record == record else { throw SatchelShaderError.invalidRequest }
                if old.wanted == mask { return 2 }
            }
            let translated = try SatchelShaderMetrics.measure("prepare-use") { try resources!.prepare(request) }
            if stage == .vertex, mask != resources!.mask(key) { try resources!.mergeWanted([key: mask]) }
            var inputs: UInt32 = 0
            if stage == .fragment, let source = translated.sources["b"] {
                let regex = try NSRegularExpression(pattern: #"\[\[user\(locn([0-9]+)\)"#)
                let text = source as NSString
                for match in regex.matches(in: source, range: NSRange(location: 0, length: text.length)) {
                    if let location = Int(text.substring(with: match.range(at: 1))), (0..<32).contains(location) { inputs |= UInt32(1) << location }
                }
            }
            let bytes = translated.sources.values.reduce(0) { $0 + $1.utf8.count } + translated.reflectionJSON.count
                + (translated.variantReflectionJSON?.values.reduce(0) { $0 + $1.count } ?? 0)
            if let old = entries[key], old.shader != nil { sourceBytes -= old.bytes }
            clock &+= 1
            entries[key] = Entry(record: record, request: translated.requestSHA256, wanted: mask, inputs: inputs,
                                 shader: translated, bytes: bytes, touched: clock)
            sourceBytes += bytes; trim()
            return 1
        } catch SatchelShaderError.unsupportedStage { return -3 }
        catch SatchelShaderError.invalidRequest { return -2 }
        catch is SatchelShaderCacheError { return -5 }
        catch { return -4 }
    }
    func inputMask(_ key: String) -> UInt32 {
        lock.lock(); defer { lock.unlock() }; return entries[key]?.inputs ?? 0
    }
    // Preparation runs on the serialized submit thread, which joins prefetch
    // lookups before changing requests. A change invalidates shader and composite
    // caches before the renderer resolves the next draw.
    private func selection(_ name: String) throws -> (SatchelTranslatedShader, String) {
        let parts = name.split(separator: "_", maxSplits: 1, omittingEmptySubsequences: false)
        guard let first = parts.first else { throw SatchelShaderError.invalidRequest }
        let key = String(first), variant = parts.count == 1 ? "b" : String(parts[1])
        guard NativePreparationRecipe.hex(key, count: 16) else { throw SatchelShaderError.invalidRequest }
        let value = try shader(key)
        guard value.sources[variant] != nil else { throw SatchelShaderError.invalidRequest }
        return (value, variant)
    }
    func reflection(_ name: String) -> NSDictionary? {
        lock.lock(); defer { lock.unlock() }
        guard let (value, variant) = try? selection(name),
              let data = try? value.reflection(variant: variant),
              let object = try? JSONSerialization.jsonObject(with: data) as? NSDictionary else { return nil }
        return object
    }
    func library(_ name: String, layer: UnsafeMutablePointer<Int32>?) -> AnyObject? {
        lock.lock()
        guard let (value, variant) = try? selection(name), let resources else { lock.unlock(); return nil }
        lock.unlock()
        do {
            let result = try SatchelShaderMetrics.measure("library-use") { try resources.compile(value, variant: variant) }
            layer?.pointee = result.layerOutput ? 1 : 0
            return result.library as AnyObject
        } catch {
            let code: Int
            if case SatchelShaderCompilationError.failed(let number) = error { code = number } else { code = -1 }
            fputs("[dm] PhoneShader key=\(value.key) variant=\(variant) compile failed code=\(code)\n", stderr)
            return nil
        }
    }
}

@_cdecl("satchel_phone_shader_prefetch")
public func phoneShaderPrefetch(_ key: UnsafePointer<CChar>?, _ control: UnsafeRawPointer?, _ controlCount: Int,
                              _ code: UnsafeRawPointer?, _ codeCount: Int) {
    guard let key, let control, let code, (0x760...0x880).contains(controlCount),
          (32...16 * 1024 * 1024).contains(codeCount) else { return }
    let name = String(cString: key)
    guard NativePreparationRecipe.hex(name, count: 16) else { return }
    autoreleasepool {
        PhoneShaderRuntime.shared.prefetch(key: name, control: Data(bytes: control, count: controlCount),
                                          code: Data(bytes: code, count: codeCount))
    }
}

@_cdecl("satchel_phone_shader_prepare")
public func phoneShaderPrepare(_ key: UnsafePointer<CChar>?, _ control: UnsafeRawPointer?, _ controlCount: Int,
                        _ code: UnsafeRawPointer?, _ codeCount: Int, _ wanted: UInt32) -> Int32 {
    guard let key, let control, let code, (0x760...0x880).contains(controlCount), (32...16 * 1024 * 1024).contains(codeCount) else { return -2 }
    let name = String(cString: key)
    guard NativePreparationRecipe.hex(name, count: 16) else { return -2 }
    return autoreleasepool { PhoneShaderRuntime.shared.prepare(key: name, control: Data(bytes: control, count: controlCount),
                                                               code: Data(bytes: code, count: codeCount), wanted: wanted) }
}
@_cdecl("satchel_phone_shader_inputs")
public func phoneShaderInputs(_ key: UnsafePointer<CChar>?) -> UInt32 {
    guard let key else { return 0 }; return PhoneShaderRuntime.shared.inputMask(String(cString: key))
}
@_cdecl("satchel_phone_shader_reflection")
public func phoneShaderReflection(_ key: UnsafePointer<CChar>?) -> UnsafeMutableRawPointer? {
    guard let key, let object = PhoneShaderRuntime.shared.reflection(String(cString: key)) else { return nil }
    return Unmanaged.passRetained(object).toOpaque()
}
@_cdecl("satchel_phone_shader_library")
public func phoneShaderLibrary(_ name: UnsafePointer<CChar>?, _ layer: UnsafeMutablePointer<Int32>?) -> UnsafeMutableRawPointer? {
    guard let name, let object = PhoneShaderRuntime.shared.library(String(cString: name), layer: layer) else { return nil }
    return Unmanaged.passRetained(object).toOpaque()
}

@_cdecl("satchel_phone_shader_session_reset")
public func phoneShaderSessionReset() { PhoneShaderRuntime.shared.resetSession() }
