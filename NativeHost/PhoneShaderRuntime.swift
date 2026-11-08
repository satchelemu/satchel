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
    // Bounded first use (DM_PSO_AHEAD=1, pipeline mode "bounded"). Recording threads reach the
    // resources through this separate lock: the submit thread holds `lock` while it waits.
    // Read at each use: the launch policy (PipelineSettings.m) sets it before a session starts.
    static var boundedMode: Bool { if let v = getenv("DM_PSO_AHEAD") { return strcmp(v, "1") == 0 }; return false }
    // Translation ahead in any pipeline mode (developer trial, DM_SHADER_AHEAD=<lanes 1..8>, set by
    // -SatchelShaderAhead): the same ahead jobs and draw-side joins as bounded mode, on that many lanes,
    // fed by BindProgram and by the submission pre-pass (graphics/TranslateAhead.inc). 0 when unset.
    static var aheadLanes: Int {
        guard let v = getenv("DM_SHADER_AHEAD"), let lanes = Int(String(cString: v)), (1...8).contains(lanes) else { return 0 }
        return lanes
    }
    static var aheadMode: Bool { boundedMode || aheadLanes > 0 }
    private let aheadLock = NSLock()
    private var aheadResources: ShaderWarmupResources?

    func resetSession() {
        lock.lock(); defer { lock.unlock() }
        entries.removeAll(keepingCapacity: false)
        resources?.endSession()
        store = nil; compiler = nil; resources = nil; sourceBytes = 0; clock = 0
        aheadLock.lock(); aheadResources = nil; aheadLock.unlock()
    }

    private func configure() throws {
        if store != nil { return }
        guard let raw = getenv("DM_PHONE_SHADER_CACHE") else {
            throw SatchelShaderError.invalidRequest
        }
        let resources = try ShaderWarmupResources.get(directory: URL(fileURLWithPath: String(cString: raw), isDirectory: true))
        self.resources = resources; store = resources.store; compiler = resources.compiler
        if Self.aheadMode {
            resources.useAheadLanes(Self.aheadLanes > 0 ? Self.aheadLanes : 2)
            aheadLock.lock(); aheadResources = resources; aheadLock.unlock()
        }
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
            let translated = try SatchelShaderMetrics.measure("prepare-use") {
                try Self.aheadMode ? resources!.prepareNow(request) : resources!.prepare(request)
            }
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
    /// Launch prewarm (DM_SATCHEL_PREWARM=1): a recorded shader the game has not handed over yet,
    /// prepared from this device's own first-use capture (DM_NVN_SHDUMP/<key>.ctl and .code) through
    /// the same `prepare` a draw calls, so its request identity is the one the draw computes. The
    /// capture keeps only shaders whose key their bytes reproduce; `prepare` checks that again.
    /// Returns `prepare`'s status and the engine's stage number (0 vertex, 1 pixel, 5 compute).
    func prepareKnown(key: String, stage: inout Int32) -> Int32 {
        guard let raw = getenv("DM_NVN_SHDUMP"), raw.pointee != 0 else { return -2 }
        let base = URL(fileURLWithPath: String(cString: raw), isDirectory: true)
        func read(_ name: String, _ range: ClosedRange<Int>) -> Data? {
            let url = base.appendingPathComponent(name)
            guard let info = try? url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]),
                  info.isRegularFile == true, info.isSymbolicLink != true, range.contains(info.fileSize ?? -1) else { return nil }
            return try? Data(contentsOf: url)
        }
        guard let control = read(key + ".ctl", 0x760...0x880), let code = read(key + ".code", 32...16 * 1024 * 1024),
              let kind = try? SatchelShaderTranslator.stage(control: control, code: code) else { return -2 }
        stage = kind == .vertex ? 0 : kind == .fragment ? 1 : 5
        return prepare(key: key, control: control, code: code, wanted: 0)
    }
    /// A hint: the game bound this program (a recording thread), or the submission pre-pass found it
    /// ahead of its draw (the submit thread, `urgent`). Never blocks behind a draw: the requests are made
    /// on the ahead lane, from value snapshots.
    func ahead(vertex: (key: String, control: Data, code: Data)?, fragment: (key: String, control: Data, code: Data)?, urgent: Bool = false) {
        guard Self.aheadMode else { return }
        aheadLock.lock(); var target = aheadResources; aheadLock.unlock()
        if target == nil {
            guard lock.try() else { return }
            do { try configure(); target = resources } catch { }
            lock.unlock()
        }
        guard let resources = target else { return }
        let first = fragment.map { f in (key: f.key, make: { () throws -> SatchelShaderRequest in
            let stage = try SatchelShaderTranslator.stage(control: f.control, code: f.code)
            // Not a pixel or compute shader: leave it to the draw.
            guard stage != .vertex else { throw SatchelShaderError.invalidRequest }
            return try SatchelShaderRequest(control: f.control, code: f.code, stage: stage, expectedKey: f.key)
        }) }
        let make = vertex.map { v in (key: v.key, make: { (mask: UInt32) throws -> SatchelShaderRequest in
            guard try SatchelShaderTranslator.stage(control: v.control, code: v.code) == .vertex else { throw SatchelShaderError.invalidRequest }
            return try SatchelShaderRequest(control: v.control, code: v.code, stage: .vertex,
                wantedPixelLocations: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 }), expectedKey: v.key)
        }) }
        resources.ahead(identity: (vertex?.key ?? "-") + "/" + (fragment?.key ?? "-"), first: first, vertex: make, urgent: urgent)
    }
    /// Whether the ahead job finished this key's translation and base library (submit thread).
    func aheadReady(_ key: String) -> Bool {
        aheadLock.lock(); let target = aheadResources; aheadLock.unlock()
        return target?.aheadIsReady(key) ?? false
    }
    func aheadProgress() -> Int32 {
        aheadLock.lock(); let target = aheadResources; aheadLock.unlock()
        return target?.aheadProgress() ?? 0
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
            let result = try SatchelShaderMetrics.measure("library-use") {
                try Self.aheadMode ? resources.compileNow(value, variant: variant) : resources.compile(value, variant: variant)
            }
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
@_cdecl("satchel_phone_shader_prepare_known")
public func phoneShaderPrepareKnown(_ key: UnsafePointer<CChar>?, _ stage: UnsafeMutablePointer<Int32>?) -> Int32 {
    guard let key, let stage else { return -2 }
    let name = String(cString: key)
    guard NativePreparationRecipe.hex(name, count: 16) else { return -2 }
    return autoreleasepool { PhoneShaderRuntime.shared.prepareKnown(key: name, stage: &stage.pointee) }
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

@_cdecl("satchel_phone_shader_ahead")
public func phoneShaderAhead(_ vertexKey: UnsafePointer<CChar>?, _ vertexControl: UnsafeRawPointer?, _ vertexControlCount: Int,
                             _ vertexCode: UnsafeRawPointer?, _ vertexCodeCount: Int,
                             _ fragmentKey: UnsafePointer<CChar>?, _ fragmentControl: UnsafeRawPointer?, _ fragmentControlCount: Int,
                             _ fragmentCode: UnsafeRawPointer?, _ fragmentCodeCount: Int, _ urgent: Int32) {
    func stage(_ key: UnsafePointer<CChar>?, _ control: UnsafeRawPointer?, _ controlCount: Int,
               _ code: UnsafeRawPointer?, _ codeCount: Int) -> (key: String, control: Data, code: Data)? {
        guard let key, let control, let code, (0x760...0x880).contains(controlCount), (32...16 * 1024 * 1024).contains(codeCount) else { return nil }
        let name = String(cString: key)
        guard NativePreparationRecipe.hex(name, count: 16) else { return nil }
        // Value snapshots: the bridge copies both ranges before it returns.
        return (name, Data(bytes: control, count: controlCount), Data(bytes: code, count: codeCount))
    }
    autoreleasepool {
        let vertex = stage(vertexKey, vertexControl, vertexControlCount, vertexCode, vertexCodeCount)
        let fragment = stage(fragmentKey, fragmentControl, fragmentControlCount, fragmentCode, fragmentCodeCount)
        guard vertex != nil || fragment != nil else { return }
        PhoneShaderRuntime.shared.ahead(vertex: vertex, fragment: fragment, urgent: urgent != 0)
    }
}
/// 1 when the key's ahead work is finished; with no key, the number of finished ahead keys (wrapping).
@_cdecl("satchel_phone_shader_ahead_ready")
public func phoneShaderAheadReady(_ key: UnsafePointer<CChar>?) -> Int32 {
    guard let key else { return PhoneShaderRuntime.shared.aheadProgress() }
    return PhoneShaderRuntime.shared.aheadReady(String(cString: key)) ? 1 : 0
}

@_cdecl("satchel_phone_shader_session_reset")
public func phoneShaderSessionReset() { PhoneShaderRuntime.shared.resetSession() }
