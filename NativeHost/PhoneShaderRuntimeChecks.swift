import Foundation
import Metal

// Private records and generated source stay in the caller's external scratch directory.
@main struct PhoneShaderRuntimeChecks {
    static func check(_ query: [String: Any]) throws -> Int {
        let record = try Data(contentsOf: URL(fileURLWithPath: query["input"] as! String))
        let key = query["key"] as! String
        let size = Int(try ShaderRecord.word(record, 0x2c))
        let control = record.subdata(in: 0x38 + size..<record.count)
        let code = record.subdata(in: 0x38..<0x38 + size)
        let wanted = UInt32(query["wanted"] as? UInt64 ?? 0)
        let runtime = PhoneShaderRuntime.shared
        runtime.resetSession()
        func prepare() -> Int32 {
            key.withCString { k in control.withUnsafeBytes { c in code.withUnsafeBytes { b in
                phoneShaderPrepare(k, c.baseAddress, control.count, b.baseAddress, code.count, wanted)
            } } }
        }
        guard prepare() == 1, prepare() == 2 else { throw SatchelShaderError.translationFailed }
        let stage = try SatchelShaderTranslator.stage(control: control, code: code)
        let request = try SatchelShaderRequest(control: control, code: code, stage: stage,
            wantedPixelLocations: stage == .vertex ? Set((0..<32).filter { wanted & (1 << $0) != 0 }) : [], expectedKey: key)
        let device = MTLCreateSystemDefaultDevice()!
        let compiler = SatchelShaderCompiler(device: device)
        let store = try SatchelShaderStore(directory: URL(fileURLWithPath: String(cString: getenv("DM_PHONE_SHADER_CACHE")!)),
            deviceIdentity: compiler.deviceIdentity)
        let identity = try SatchelShaderTranslator.identity(request, normalized: SatchelShaderTranslator.normalized(request))
        guard let value = try store.load(requestSHA256: identity) else { throw SatchelShaderCacheError.corruptEntry }
        for variant in value.sources.keys.sorted() {
            let name = key + "_" + variant
            let expected = try JSONSerialization.jsonObject(with: value.reflection(variant: variant)) as! NSDictionary
            guard let pointer = name.withCString({ phoneShaderReflection($0) }) else { throw SatchelShaderError.invalidRequest }
            let actual = Unmanaged<NSDictionary>.fromOpaque(pointer).takeRetainedValue()
            guard actual == expected else { throw SatchelShaderCacheError.corruptEntry }
            if query["compile"] as? Bool == true {
                guard let pointer = name.withCString({ phoneShaderLibrary($0, nil) }) else { throw SatchelShaderError.translationFailed }
                let library = Unmanaged<AnyObject>.fromOpaque(pointer).takeRetainedValue() as! MTLLibrary
                guard let function = library.makeFunction(name: expected["entry"] as! String) else { throw SatchelShaderError.invalidRequest }
                if stage == .geometry && variant != "bgv" { _ = try device.makeComputePipelineState(function: function) }
            }
        }
        guard runtime.reflection(key) == runtime.reflection(key + "_b"),
              runtime.reflection("") == nil,
              runtime.reflection(key + "_missing") == nil,
              runtime.reflection(key + "_") == nil,
              runtime.library(key + "_missing", layer: nil) == nil else { throw SatchelShaderError.invalidRequest }
        runtime.resetSession()
        guard prepare() == 1, runtime.reflection(key + "_b") != nil else { throw SatchelShaderCacheError.corruptEntry }
        try JSONEncoder().encode(value).write(to: URL(fileURLWithPath: query["output"] as! String))
        return value.sources.count
    }
    static func main() {
        while let line = readLine() {
            autoreleasepool {
                do {
                    let query = try JSONSerialization.jsonObject(with: Data(line.utf8)) as! [String: Any]
                    let count = try check(query)
                    print("{\"status\":\"ok\",\"variants\":\(count)}")
                } catch {
                    // Compiler diagnostics may contain private shader source.
                    print("{\"status\":\"failed\"}")
                }
            }
            fflush(stdout)
        }
    }
}
