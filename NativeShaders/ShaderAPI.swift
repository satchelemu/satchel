import Foundation
import CryptoKit

public enum SatchelShaderStage: String, Codable, Sendable {
    case vertex = "vs", fragment = "ps", compute = "cs"
    case geometry = "gs", hull = "hs", domain = "ds"
}

public enum SatchelShaderError: Error {
    case invalidRequest, unsupportedStage, cancelled, translationFailed
}

// A value snapshot. Runtime callers must copy control/code while NVN owns them.
public struct SatchelShaderRequest: Sendable {
    public let record: Data
    public let stage: SatchelShaderStage
    public let factsJSON: Data?
    public let wantedPixelLocations: Set<Int>
    public let mappingOnly: Bool
    public let dualSource: Bool
    public let skippedControlRanges: [Range<Int>]
    public let expectedKey: String?

    public init(record: Data, stage: SatchelShaderStage, factsJSON: Data? = nil,
                wantedPixelLocations: Set<Int> = [], mappingOnly: Bool = false,
                dualSource: Bool = false, skippedControlRanges: [Range<Int>] = [],
                expectedKey: String? = nil) {
        self.record = record; self.stage = stage; self.factsJSON = factsJSON
        self.wantedPixelLocations = wantedPixelLocations; self.mappingOnly = mappingOnly
        self.dualSource = dualSource; self.skippedControlRanges = skippedControlRanges
        self.expectedKey = expectedKey
    }

    public init(control: Data, code: Data, stage: SatchelShaderStage, factsJSON: Data? = nil,
                wantedPixelLocations: Set<Int> = [], mappingOnly: Bool = false,
                dualSource: Bool = false, skippedControlRanges: [Range<Int>] = [],
                expectedKey: String? = nil) throws {
        guard control.count >= 0x760, control.count <= 1024 * 1024,
              code.count >= 32, code.count <= 16 * 1024 * 1024 else {
            throw SatchelShaderError.invalidRequest
        }
        var record = Data("BTCDBTCD".utf8) + Data(count: 0x30)
        ShaderRecord.put(UInt32(control.count), &record, 0x28)
        ShaderRecord.put(UInt32(code.count), &record, 0x2c)
        record.append(code); record.append(control)
        self.init(record: record, stage: stage, factsJSON: factsJSON,
                  wantedPixelLocations: wantedPixelLocations, mappingOnly: mappingOnly,
                  dualSource: dualSource, skippedControlRanges: skippedControlRanges, expectedKey: expectedKey)
    }
}

public struct SatchelTranslatedShader: Codable, Sendable {
    public let key: String
    public let recordSHA256: String
    public let requestSHA256: String
    // Ordinary suffixes b, bv, bp, bx, bf; geometry b, bg0...bg4, bgv.
    public let sources: [String: String]
    public let reflectionJSON: Data
    public let constantBufferExtents: [Int: Int]
    public let unwrittenStorageBindings: [Int]
    public let c1Unproven: Bool
    // Geometry kernels and the vertex reader have distinct binding contracts.
    public var variantReflectionJSON: [String: Data]? = nil

    public func reflection(variant: String = "b") throws -> Data {
        guard sources[variant] != nil else { throw SatchelShaderError.invalidRequest }
        return variantReflectionJSON?[variant] ?? reflectionJSON
    }

    static let allowedVariants: Set<String> = ["b", "bv", "bp", "bx", "bf", "bg0", "bg1", "bg2", "bg3", "bg4", "bgv"]
}

public enum SatchelShaderTranslator {
    // Bump when source transforms, query semantics or bridge options change.
    public static let implementationID = "satchel-native-v3-side-26dc2fa9477c-f11ba9f0"

    static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func normalized(_ request: SatchelShaderRequest) throws -> NormalizedShader {
        guard request.record.count <= 17 * 1024 * 1024 + 0x38,
              request.wantedPixelLocations.allSatisfy({ (0..<32).contains($0) }),
              request.skippedControlRanges.count <= 16,
              (request.factsJSON?.count ?? 0) <= 1024 * 1024 else {
            throw SatchelShaderError.invalidRequest
        }
        var facts: [String: Any]?
        if let json = request.factsJSON {
            guard let value = try JSONSerialization.jsonObject(with: json) as? [String: Any],
                  Set(value.keys).isSubset(of: ["tex", "img", "attr", "fout", "local", "shared", "early_fragment_tests", "debug", "reduced", "softfloat64"]) else {
                throw SatchelShaderError.invalidRequest
            }
            facts = value
        }
        if request.stage == .compute {
            let codeSize = Int(try ShaderRecord.word(request.record, 0x2c))
            let offset = 0x38 + codeSize
            let xyz = try (0..<3).map { Int(try ShaderRecord.word(request.record, offset + 0x748 + 4 * $0)) }
            let shared = Int(try ShaderRecord.word(request.record, offset + 0x754))
            guard xyz.allSatisfy({ $0 > 0 && $0 <= 1024 }), xyz.reduce(1, *) <= 1024,
                  shared <= 96 * 1024 else { throw SatchelShaderError.invalidRequest }
            var computeFacts: [String: Any] = ["local": xyz, "shared": (shared + 15) / 16 * 16]
            if let facts { computeFacts.merge(facts) { _, new in new } }
            facts = computeFacts
        }
        return try ShaderRecord.normalize(request.record, stage: request.stage.rawValue, facts: facts,
                                          expectedKey: request.expectedKey,
                                          skippedControlRanges: request.skippedControlRanges)
    }

    public static func stage(control: Data, code: Data) throws -> SatchelShaderStage {
        guard control.count >= 0x760, code.count >= 32,
              try ShaderRecord.word(control, 0) == 0x98761234 else {
            throw SatchelShaderError.invalidRequest
        }
        let magic = try ShaderRecord.word(code, 0)
        if magic == 0x12345679 { return .compute }
        if magic != 0x12345678 {
            let xyz = try (0..<3).map { try ShaderRecord.word(control, 0x748 + 4 * $0) }
            guard xyz.allSatisfy({ $0 > 0 }) else { throw SatchelShaderError.invalidRequest }
            return .compute
        }
        let word = try ShaderRecord.word(code, 0x30), type = (word >> 10) & 15
        if type == 0 && word & 31 == 1 { return .vertex }
        guard let stage = [1: SatchelShaderStage.vertex, 2: .hull, 3: .domain, 4: .geometry, 5: .fragment][Int(type)] else {
            throw SatchelShaderError.invalidRequest
        }
        return stage
    }

    static func identity(_ request: SatchelShaderRequest, normalized: NormalizedShader) throws -> String {
        let metadata: [String: Any] = ["implementation": implementationID, "job": normalized.job,
            "record": sha256(request.record), "wanted": request.wantedPixelLocations.sorted(),
            "mappingOnly": request.mappingOnly, "dualSource": request.dualSource]
        return sha256(try JSONSerialization.data(withJSONObject: metadata, options: [.sortedKeys]))
    }

    // Call on a bounded worker queue. Cancellation is observed between native calls.
    public static func translate(_ request: SatchelShaderRequest,
                                 cancelled: () -> Bool = { false }) throws -> SatchelTranslatedShader {
        try autoreleasepool {
            if cancelled() { throw SatchelShaderError.cancelled }
            guard [.vertex, .fragment, .compute, .geometry].contains(request.stage) else {
                throw SatchelShaderError.unsupportedStage
            }
            do {
                let normalized = try normalized(request)
                let codeSize = Int(try ShaderRecord.word(request.record, 0x2c))
                guard try stage(control: request.record.subdata(in: 0x38 + codeSize..<request.record.count),
                                code: request.record.subdata(in: 0x38..<0x38 + codeSize)) == request.stage else {
                    throw SatchelShaderError.invalidRequest
                }
                let identity = try identity(request, normalized: normalized)
                if cancelled() { throw SatchelShaderError.cancelled }
                let result = try NativeShaderTranslation.prepare(code: normalized.code, job: normalized.job,
                    wanted: request.wantedPixelLocations, mappingOnly: request.mappingOnly, dual: request.dualSource, cancelled: cancelled)
                if cancelled() { throw SatchelShaderError.cancelled }
                guard let base = result.sources["b"], let bindings = result.reflection["bindings"] as? [[String: Any]] else {
                    throw SatchelShaderError.translationFailed
                }
                return SatchelTranslatedShader(key: normalized.key, recordSHA256: sha256(request.record),
                    requestSHA256: identity, sources: result.sources,
                    reflectionJSON: try ShaderReflectionJSON.encode(result.reflection),
                    constantBufferExtents: request.stage == .geometry ? [:] : ShaderBufferAnalysis.extents(base, bindings: bindings),
                    unwrittenStorageBindings: request.stage == .geometry ? [] : ShaderBufferAnalysis.unwritten(base, bindings: bindings),
                    c1Unproven: (request.stage == .geometry ? Array(result.sources.values) : [base]).contains {
                        ShaderBufferAnalysis.c1Unproven($0, bytes: (normalized.job["c1"] as? String ?? "").count / 2)
                    }, variantReflectionJSON: result.variantReflections.isEmpty ? nil : try result.variantReflections.mapValues { try ShaderReflectionJSON.encode($0) })
            } catch let error as SatchelShaderError { throw error }
            catch { throw cancelled() ? SatchelShaderError.cancelled : SatchelShaderError.translationFailed }
        }
    }
}
